#include "systems/renderSystem.h"
#include "components/hidden.h"
#include "components/light.h"
#include "components/model.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/particleSystem.h"
#include "systems/resourceSystem.h"
#include "systems/transformSystem.h"
#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <iterator>
#include <string>
#include <tuple>

namespace {
struct TransparentDraw {
  Entity entity;
  uint32_t indexStart;
  uint32_t indexCount;
  uint32_t materialHandle;
  float distance;
  uint32_t shaderHandle;
};

// Calls fn(indexStart, indexCount, materialHandle) for each run of parts that are contiguous in the index buffer and
// currently use the same material, among the parts whose material handle passes `include`. Parts sharing a material
// are laid out together, so this is usually one draw per material; a part given a material of its own just becomes
// its own run. With `singlePartIncluded`, a single-part mesh skips `include` (it saves a material lookup per entity
// when the caller knows the answer).
template <typename Include, typename Fn>
void forEachDrawRange(const Mesh &mesh, const Model &model, bool singlePartIncluded, Include &&include, Fn &&fn) {
  const auto &submeshes = mesh.getSubmeshes();
  auto materialOf = [&](uint32_t part) { return part < model.materialHandles.size() ? model.materialHandles[part] : 0; };

  if (submeshes.size() == 1) {
    if (singlePartIncluded || include(materialOf(0)))
      fn(submeshes[0].indexStart, submeshes[0].indexCount, materialOf(0));
    return;
  }

  bool open = false;
  uint32_t start = 0, end = 0, material = 0;

  for (uint32_t index : mesh.getDrawOrder()) {
    const Submesh &part = submeshes[index];
    const uint32_t partMaterial = materialOf(index);
    if (!include(partMaterial)) {
      if (open)
        fn(start, end - start, material);
      open = false;
      continue;
    }

    if (open && part.indexStart == end && partMaterial == material) {
      end += part.indexCount;
      continue;
    }
    if (open)
      fn(start, end - start, material);
    open = true;
    start = part.indexStart;
    end = part.indexStart + part.indexCount;
    material = partMaterial;
  }
  if (open)
    fn(start, end - start, material);
}

// Entities without a Transform or marked Hidden are never drawn.
bool isHidden(ComponentManager &componentManager, Entity entity) {
  return componentManager.containsComponent<Hidden>(entity) || !componentManager.containsComponent<Transform>(entity);
}

// Frustum planes (ax + by + cz + d >= 0 inside), extracted from a view-projection matrix
struct Frustum {
  glm::vec4 planes[6];

  explicit Frustum(const glm::mat4 &viewProjection) {
    const glm::mat4 m = glm::transpose(viewProjection);
    planes[0] = m[3] + m[0];
    planes[1] = m[3] - m[0];
    planes[2] = m[3] + m[1];
    planes[3] = m[3] - m[1];
    planes[4] = m[3] + m[2];
    planes[5] = m[3] - m[2];
    for (auto &plane : planes)
      plane /= glm::length(glm::vec3(plane));
  }

  bool intersectsSphere(const glm::vec3 &center, float radius) const {
    for (const auto &plane : planes)
      if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
        return false;
    return true;
  }
};

// The largest scale along any axis: rotation keeps a bounding sphere's radius, scale stretches it by this much
float maxScaleOf(const glm::mat4 &modelMatrix) {
  return glm::sqrt(glm::max(glm::max(glm::dot(glm::vec3(modelMatrix[0]), glm::vec3(modelMatrix[0])),
                                     glm::dot(glm::vec3(modelMatrix[1]), glm::vec3(modelMatrix[1]))),
                            glm::dot(glm::vec3(modelMatrix[2]), glm::vec3(modelMatrix[2]))));
}

// Mesh bounds moved to world space
bool isMeshVisible(const Frustum &frustum, const Mesh &mesh, const glm::mat4 &modelMatrix) {
  const glm::vec3 center = glm::vec3(modelMatrix * glm::vec4(mesh.getBoundsCenter(), 1.0f));
  return frustum.intersectsSphere(center, mesh.getBoundsRadius() * maxScaleOf(modelMatrix));
}

glm::mat3 calculateNormalMatrix(const glm::mat4 &modelMatrix) {
  return glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
}

// Uniform names for each sun shadow cascade, built once
struct CascadeUniforms {
  std::string matrix, split, texel;
};
const CascadeUniforms kCascadeUniforms[4] = {
    {"cascadeMatrices[0]", "cascadeSplits[0]", "cascadeTexels[0]"},
    {"cascadeMatrices[1]", "cascadeSplits[1]", "cascadeTexels[1]"},
    {"cascadeMatrices[2]", "cascadeSplits[2]", "cascadeTexels[2]"},
    {"cascadeMatrices[3]", "cascadeSplits[3]", "cascadeTexels[3]"},
};
static_assert(EngineConfig::SHADOW_CASCADES >= 1 && EngineConfig::SHADOW_CASCADES <= 4,
              "the shader's cascade arrays hold 4");

// Texture units 0-2 hold material textures and 4 the directional shadow map; point shadow cube maps follow
constexpr int kFirstPointShadowUnit = 5;
constexpr const char *kPointShadowSamplers[] = {"pointShadowMaps0", "pointShadowMaps1", "pointShadowMaps2",
                                                "pointShadowMaps3"};
static_assert(std::size(kPointShadowSamplers) == EngineConfig::MAX_POINT_SHADOWS,
              "one sampler name (and one sampler in fragmentShader.frag) per point shadow slot");

// Cube faces in GL order (+X, -X, +Y, -Y, +Z, -Z): where each looks and which way is up, as cube maps expect
const glm::vec3 kCubeFaceDirections[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const glm::vec3 kCubeFaceUps[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
constexpr float kPointShadowNear = 0.05f;

constexpr float kSoftOpacityEnter = 0.95f;
constexpr float kSoftOpacityExit = 0.90f;
} // namespace

void RenderSystem::setShadowShaderHandle(uint32_t handle) { m_shadowShaderHandle = handle; }

template <typename SameRun, typename DrawRun> void RenderSystem::drawInstanceRuns(SameRun sameRun, DrawRun drawRun) {
  auto byKey = [](const InstanceItem &a, const InstanceItem &b) {
    return std::tie(a.meshHandle, a.indexStart, a.indexCount, a.materialHandle) <
           std::tie(b.meshHandle, b.indexStart, b.indexCount, b.materialHandle);
  };
  std::sort(m_instanceItems.begin(), m_instanceItems.end(), byKey);

  for (size_t start = 0; start < m_instanceItems.size();) {
    size_t end = start + 1;
    while (end < m_instanceItems.size() && sameRun(m_instanceItems[start], m_instanceItems[end]))
      ++end;

    m_instanceData.clear();
    for (size_t i = start; i < end; ++i)
      m_instanceData.push_back(m_instanceItems[i].instance);

    const int count = static_cast<int>(end - start);
    m_renderer.uploadInstances(m_instanceData.data(), count);
    drawRun(m_instanceItems[start], count);
    start = end;
  }
}

void RenderSystem::markBatchesDirty() { m_batchesDirty = true; }

void RenderSystem::gatherShadowCasters(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                                       TransformSystem &transformSystem) {
  if (m_shadowCastersGathered)
    return;
  m_shadowCastersGathered = true;
  m_shadowCasters.clear();
  componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
    if (isHidden(componentManager, entity))
      return;
    const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
    const glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(componentManager.getOrThrow<Transform>(entity));
    m_shadowCasters.push_back({model.meshHandle, modelMatrix,
                               glm::vec3(modelMatrix * glm::vec4(mesh.getBoundsCenter(), 1.0f)),
                               mesh.getBoundsRadius() * maxScaleOf(modelMatrix)});
  });
  // Sorted once, so every pass finds the instances of a mesh next to each other
  std::sort(m_shadowCasters.begin(), m_shadowCasters.end(),
            [](const ShadowCaster &a, const ShadowCaster &b) { return a.meshHandle < b.meshHandle; });
}

void RenderSystem::drawShadowCasters(Shader &depthShader, const glm::mat4 &viewProjection,
                                     ResourceSystem &resourceSystem) {
  const Frustum frustum(viewProjection);
  depthShader.setMat4("lightSpaceMatrix", viewProjection);
  const bool instanced = depthShader.supportsInstancing();
  depthShader.setInt("useInstancing", instanced ? 1 : 0);

  // Instances of one mesh go out together; the list is sorted by mesh
  uint32_t runMesh = 0;
  m_instanceData.clear();
  auto flush = [&] {
    if (m_instanceData.empty())
      return;
    m_renderer.uploadInstances(m_instanceData.data(), static_cast<int>(m_instanceData.size()));
    m_renderer.drawMeshInstanced(resourceSystem.getMesh(runMesh), static_cast<int>(m_instanceData.size()));
    m_instanceData.clear();
  };

  for (const ShadowCaster &caster : m_shadowCasters) {
    if (!frustum.intersectsSphere(caster.center, caster.radius))
      continue;
    if (!instanced) {
      depthShader.setMat4("model", caster.model);
      m_renderer.drawMesh(resourceSystem.getMesh(caster.meshHandle));
      continue;
    }
    if (caster.meshHandle != runMesh)
      flush();
    runMesh = caster.meshHandle;
    m_instanceData.push_back({caster.model, {}});
  }
  flush();
}

RenderSystem::DirectionalShadow
RenderSystem::renderDirectionalShadow(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                                      TransformSystem &transformSystem, LightSystem &lightSystem, const Camera &camera,
                                      const glm::mat4 &view) {
  DirectionalShadow shadow;
  const Light *sun = nullptr;
  for (Entity lightEntity : lightSystem.getLights()) {
    const Light &light = componentManager.getOrThrow<Light>(lightEntity);
    if (light.type == LightType::Directional) {
      sun = &light;
      break;
    }
  }
  if (!sun || m_shadowShaderHandle == 0)
    return shadow;
  shadow.active = true;

  gatherShadowCasters(componentManager, resourceSystem, transformSystem);
  constexpr int cascades = EngineConfig::SHADOW_CASCADES;
  constexpr float size = static_cast<float>(EngineConfig::SHADOW_CASCADE_SIZE);
  const float nearPlane = EngineConfig::CAMERA_NEAR;
  const float farPlane = std::min(EngineConfig::SHADOW_DISTANCE, EngineConfig::CAMERA_FAR);
  const glm::mat4 cameraToWorld = glm::inverse(view);
  const float tanY = std::tan(glm::radians(camera.fov) * 0.5f);
  const float tanX = tanY * camera.aspectRatio;
  const glm::vec3 lightDir = glm::normalize(sun->direction);
  const glm::vec3 up = std::abs(lightDir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);

  Shader &depthShader = resourceSystem.getShader(m_shadowShaderHandle);
  depthShader.use();

  float sliceNear = nearPlane;
  for (int cascade = 0; cascade < cascades; ++cascade) {
    // Split between even and logarithmic spacing: logarithmic keeps the texel size proportional to distance
    const float t = static_cast<float>(cascade + 1) / cascades;
    const float even = nearPlane + (farPlane - nearPlane) * t;
    const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, t);
    const float sliceFar = glm::mix(even, logarithmic, EngineConfig::SHADOW_CASCADE_SPLIT);

    // The slice's corners in the world, and a sphere around them. A sphere keeps the map the same size however the
    // camera turns, so shadow edges do not swim; rounding the radius keeps it from changing by tiny amounts.
    glm::vec3 corners[8];
    glm::vec3 center(0.0f);
    for (int i = 0; i < 8; ++i) {
      const float depth = (i & 4) ? sliceFar : sliceNear;
      const glm::vec4 local((i & 1 ? 1.0f : -1.0f) * depth * tanX, (i & 2 ? 1.0f : -1.0f) * depth * tanY, -depth, 1.0f);
      corners[i] = glm::vec3(cameraToWorld * local);
      center += corners[i] / 8.0f;
    }
    float radius = 0.0f;
    for (const glm::vec3 &corner : corners)
      radius = std::max(radius, glm::length(corner - center));
    radius = std::ceil(radius * 16.0f) / 16.0f;

    // Looking along the light from just outside the sphere, pulled back further so casters up to
    // SHADOW_CASTER_REACH beyond it still land in the map
    const float reach = EngineConfig::SHADOW_CASTER_REACH;
    const glm::mat4 lightView = glm::lookAt(center - lightDir * (radius + reach), center, up);
    glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + reach);

    // Snap to whole texels: as the camera moves the map slides in texel steps instead of re-sampling the scene
    // a fraction of a texel off each frame, which makes edges shimmer
    const glm::vec4 origin = lightProjection * lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec2 texelOrigin = glm::vec2(origin) * (size * 0.5f);
    const glm::vec2 snap = (glm::round(texelOrigin) - texelOrigin) * (2.0f / size);
    lightProjection[3][0] += snap.x;
    lightProjection[3][1] += snap.y;

    shadow.matrices[cascade] = lightProjection * lightView;
    shadow.splits[cascade] = sliceFar;
    shadow.texels[cascade] = 2.0f * radius / size;

    m_renderer.beginShadowPass(cascade);
    drawShadowCasters(depthShader, shadow.matrices[cascade], resourceSystem);
    sliceNear = sliceFar;
  }
  m_renderer.endShadowPass();
  return shadow;
}

namespace {
// 64-bit FNV-1a over raw bytes, then a final mix so nearby inputs spread over all bits
uint64_t hashBytes(const void *data, size_t size, uint64_t hash = 1469598103934665603ull) {
  const auto *bytes = static_cast<const unsigned char *>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  hash ^= hash >> 33;
  hash *= 0xff51afd7ed558ccdull;
  return hash ^ (hash >> 33);
}
} // namespace

// The light's position and range, plus the mesh and matrix of every caster within range: everything the cube map
// depends on. Casters are combined by addition, so the order the ECS lists them in does not matter.
uint64_t RenderSystem::pointShadowSignature(const PointShadowCaster &light) const {
  struct LightKey {
    glm::vec3 position;
    float range;
  } lightKey{light.position, light.range};
  uint64_t casters = 0;
  uint32_t count = 0;
  for (const ShadowCaster &caster : m_shadowCasters) {
    if (glm::length(caster.center - light.position) > caster.radius + light.range)
      continue;
    const uint64_t meshHash = hashBytes(&caster.meshHandle, sizeof(caster.meshHandle));
    casters += hashBytes(&caster.model, sizeof(caster.model), meshHash);
    ++count;
  }
  uint64_t signature = hashBytes(&lightKey, sizeof(lightKey));
  signature = hashBytes(&casters, sizeof(casters), signature);
  signature = hashBytes(&count, sizeof(count), signature);
  return signature == 0 ? 1 : signature; // 0 means "nothing rendered yet"
}

void RenderSystem::renderPointShadows(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                                      TransformSystem &transformSystem,
                                      const std::vector<PointShadowCaster> &casters) {
  m_pointShadowCount = static_cast<int>(casters.size());
  m_pointShadowUpdates = 0;
  if (casters.empty() || m_shadowShaderHandle == 0)
    return;

  gatherShadowCasters(componentManager, resourceSystem, transformSystem);
  Shader &depthShader = resourceSystem.getShader(m_shadowShaderHandle);
  bool rendering = false;
  for (const PointShadowCaster &caster : casters) {
    // Unchanged since this slot's cube map was rendered: keep it
    const uint64_t signature = pointShadowSignature(caster);
    if (signature == m_pointShadowSignatures[caster.slot])
      continue;
    m_pointShadowSignatures[caster.slot] = signature;
    ++m_pointShadowUpdates;
    if (!rendering) {
      depthShader.use();
      rendering = true;
    }

    // The far plane is the light's range; the shader rebuilds the same depth from the distance to compare
    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, kPointShadowNear, caster.range);
    for (int face = 0; face < 6; ++face) {
      const glm::mat4 view =
          glm::lookAt(caster.position, caster.position + kCubeFaceDirections[face], kCubeFaceUps[face]);
      m_renderer.beginPointShadowFace(caster.slot, face);
      drawShadowCasters(depthShader, projection * view, resourceSystem);
    }
  }
  if (rendering)
    m_renderer.endShadowPass();
}

void RenderSystem::rebuildRenderBatches(ComponentManager &componentManager, ResourceSystem &resourceSystem) {
  m_renderBatches.clear();

  componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
    if (isHidden(componentManager, entity) || model.opacity < kSoftOpacityEnter)
      return;

    // One entry per shader the entity's parts use
    for (size_t i = 0; i < model.materialHandles.size(); ++i) {
      RenderBatch &batch = m_renderBatches[resourceSystem.getMaterial(model.materialHandles[i]).getShaderHandle()];
      if (batch.empty() || batch.back() != entity)
        batch.push_back(entity);
    }
  });

  m_batchesDirty = false;
}

void RenderSystem::renderPipeline(SystemManager &systemManager, ComponentManager &componentManager) {
  auto &renderer = getRenderer();
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  auto &lightSystem = systemManager.getSystem<LightSystem>();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();

  Entity cameraEntity = cameraSystem.getActiveCamera();
  if (cameraEntity == -1)
    return;

  const auto &camera = componentManager.getOrThrow<Camera>(cameraEntity);
  glm::mat4 view = cameraSystem.getViewMatrix(camera);
  glm::mat4 projection = cameraSystem.getProjectionMatrix(camera);
  // From the view matrix rather than Camera::position, which a relative camera offsets by its Transform
  const glm::vec3 viewPos = glm::vec3(glm::inverse(view)[3]);
  const Frustum frustum(projection * view);
  m_culledCount = 0;

  auto &state = systemManager.getSystem<StateSystem>();
  const bool shadowsEnabled = state.isToggled(Toggle::Shadows);
  const bool gammaCorrect = state.isToggled(Toggle::GammaCorrect);

  // Shadow passes
  m_shadowCastersGathered = false;
  DirectionalShadow sunShadow;
  if (shadowsEnabled)
    sunShadow = renderDirectionalShadow(componentManager, resourceSystem, transformSystem, lightSystem, camera, view);
  const auto &pointShadows =
      lightSystem.updatePointShadows(componentManager, shadowsEnabled, EngineConfig::MAX_POINT_SHADOWS);
  renderPointShadows(componentManager, resourceSystem, transformSystem, pointShadows);

  // Main pass - grouped by shader
  if (m_batchesDirty) {
    rebuildRenderBatches(componentManager, resourceSystem);
  }

  auto configureShader = [&](Shader &shader) {
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);
    shader.setVec3("viewPos", viewPos);
    shader.setInt("useShadows", sunShadow.active ? 1 : 0);
    shader.setInt("cascadeCount", EngineConfig::SHADOW_CASCADES);
    shader.setVec3("viewForward", glm::normalize(camera.front));
    for (int cascade = 0; cascade < EngineConfig::SHADOW_CASCADES; ++cascade) {
      shader.setMat4(kCascadeUniforms[cascade].matrix, sunShadow.matrices[cascade]);
      shader.setFloat(kCascadeUniforms[cascade].split, sunShadow.splits[cascade]);
      shader.setFloat(kCascadeUniforms[cascade].texel, sunShadow.texels[cascade]);
    }
    shader.setTex("shadowMap", renderer.getDepthMap(), 4, GL_TEXTURE_2D_ARRAY);
    // Every cube sampler gets its own unit and a cube map, even unused slots (see Renderer::getPointShadowMap)
    for (int slot = 0; slot < EngineConfig::MAX_POINT_SHADOWS; ++slot)
      shader.setTex(kPointShadowSamplers[slot], renderer.getPointShadowMap(slot), kFirstPointShadowUnit + slot,
                    GL_TEXTURE_CUBE_MAP);
    shader.setFloat("pointShadowTexel", 2.0f / static_cast<float>(EngineConfig::POINT_SHADOW_SIZE));
    shader.setInt("gammaCorrect", gammaCorrect ? 1 : 0);

    lightSystem.uploadLightsToShader(shader, componentManager);
  };

  auto applyMaterial = [&](Shader &shader, const Material &material, float opacity) {
    shader.setTex("material.diffuse", material.getDiffuse(), 0);
    shader.setTex("material.specular", material.getSpecular(), 1);
    // material.normal disabled until TBN is implemented
    shader.setTex("material.emission", material.getEmission(), 2);
    shader.setFloat("material.shininess", material.getShininess());
    shader.setVec3("material.emissionColor", material.getEmissionColor());
    shader.setFloat("material.emissionStrength", material.getEmissionStrength());
    shader.setInt("useSolidDiffuseColor", material.hasDiffuseTexture() ? 0 : 1);
    shader.setVec3("solidDiffuseColor", material.getDiffuseColor());
    shader.setFloat("opacity", opacity);
    shader.setVec4("baseColorFactor", material.getBaseColorFactor());
    shader.setInt("alphaMode", static_cast<int>(material.getAlphaMode()));
    shader.setFloat("alphaCutoff", material.getAlphaCutoff());
    shader.setInt("doubleSided", material.isDoubleSided() ? 1 : 0);
    shader.setInt("unlit", material.isUnlit() ? 1 : 0);
    renderer.setFaceCulling(!material.isDoubleSided());
  };

  // Blending parts are left to the transparent pass, even on opaque models
  const bool anyBlend = resourceSystem.hasBlendMaterials();
  auto isBlend = [&](uint32_t material) {
    return resourceSystem.getMaterial(material).getAlphaMode() == AlphaMode::Blend;
  };

  // Per-draw path: shaders without instancing support and transparent models
  auto drawRange = [&](Shader &shader, Entity entity, uint32_t indexStart, uint32_t indexCount,
                       uint32_t materialHandle, float opacity) {
    const auto *transform = componentManager.getOrNil<Transform>(entity);
    const auto *model = componentManager.getOrNil<Model>(entity);
    if (!transform || !model)
      return;

    const Mesh &mesh = resourceSystem.getMesh(model->meshHandle);
    glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(*transform);
    if (!isMeshVisible(frustum, mesh, modelMatrix)) {
      ++m_culledCount;
      return;
    }

    shader.setInt("useInstancing", 0);
    shader.setMat4("MVP", projection * view * modelMatrix);
    shader.setMat4("model", modelMatrix);
    shader.setMat3("normalMatrix", calculateNormalMatrix(modelMatrix));
    applyMaterial(shader, resourceSystem.getMaterial(materialHandle), opacity);

    renderer.drawIndexRange(mesh, indexStart, indexCount);
  };

  for (auto &[shaderHandle, batch] : m_renderBatches) {
    Shader &shader = resourceSystem.getShader(shaderHandle);
    shader.use();
    configureShader(shader);

    // Parts drawn by this batch: same shader, not blending. A single-part entity is only in its shader's batch,
    // so it needs no check unless some material blends.
    const uint32_t batchShader = shaderHandle;
    auto includeOpaque = [&](uint32_t material) {
      const Material &m = resourceSystem.getMaterial(material);
      return m.getShaderHandle() == batchShader && m.getAlphaMode() != AlphaMode::Blend;
    };

    if (!shader.supportsInstancing()) {
      for (Entity entity : batch) {
        const auto *model = componentManager.getOrNil<Model>(entity);
        if (!model)
          continue;
        forEachDrawRange(resourceSystem.getMesh(model->meshHandle), *model, !anyBlend, includeOpaque,
                         [&](uint32_t start, uint32_t count, uint32_t material) {
                           drawRange(shader, entity, start, count, material, 1.0f);
                         });
      }
      continue;
    }

    // Opaque draws of the same mesh range with the same material go out as one instanced call
    m_instanceItems.clear();
    for (Entity entity : batch) {
      auto *transform = componentManager.getOrNil<Transform>(entity);
      auto *model = componentManager.getOrNil<Model>(entity);
      if (!transform || !model)
        continue;

      const Mesh &mesh = resourceSystem.getMesh(model->meshHandle);
      glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(*transform);
      if (!isMeshVisible(frustum, mesh, modelMatrix)) {
        ++m_culledCount;
        continue;
      }

      const InstanceData instance{modelMatrix, calculateNormalMatrix(modelMatrix)};
      forEachDrawRange(mesh, *model, !anyBlend, includeOpaque,
                       [&](uint32_t start, uint32_t count, uint32_t material) {
                         m_instanceItems.push_back({model->meshHandle, start, count, material, instance});
                       });
    }

    shader.setInt("useInstancing", 1);
    drawInstanceRuns(
        [](const InstanceItem &a, const InstanceItem &b) {
          return a.meshHandle == b.meshHandle && a.indexStart == b.indexStart && a.indexCount == b.indexCount &&
                 a.materialHandle == b.materialHandle;
        },
        [&](const InstanceItem &first, int count) {
          applyMaterial(shader, resourceSystem.getMaterial(first.materialHandle), 1.0f);
          renderer.drawIndexRangeInstanced(resourceSystem.getMesh(first.meshHandle), first.indexStart,
                                           first.indexCount, count);
        });
  }

  std::vector<TransparentDraw> softDraws;
  std::vector<TransparentDraw> transparentDraws;

  componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
    const bool transparentModel = model.opacity < kSoftOpacityEnter;
    if (isHidden(componentManager, entity) || (!transparentModel && !anyBlend))
      return;

    const auto &transform = componentManager.getOrThrow<Transform>(entity);
    const float distance = glm::length(camera.position - transform.position);
    auto &draws = transparentModel && model.opacity >= kSoftOpacityExit ? softDraws : transparentDraws;

    // Transparent models draw every part here; opaque models only their blending parts
    auto include = [&](uint32_t material) { return transparentModel || isBlend(material); };
    forEachDrawRange(resourceSystem.getMesh(model.meshHandle), model, transparentModel, include,
                     [&](uint32_t start, uint32_t count, uint32_t material) {
                       draws.push_back({entity, start, count, material, distance,
                                        resourceSystem.getMaterial(material).getShaderHandle()});
                     });
  });

  auto renderSorted = [&](std::vector<TransparentDraw> &draws, void (Renderer::*begin)(), void (Renderer::*end)()) {
    if (draws.empty())
      return;

    std::sort(draws.begin(), draws.end(),
              [](const TransparentDraw &a, const TransparentDraw &b) { return a.distance > b.distance; });

    (renderer.*begin)();
    for (const auto &draw : draws) {
      Shader &shader = resourceSystem.getShader(draw.shaderHandle);
      shader.use();
      configureShader(shader);
      const auto *model = componentManager.getOrNil<Model>(draw.entity);
      drawRange(shader, draw.entity, draw.indexStart, draw.indexCount, draw.materialHandle,
                model ? model->opacity : 1.0f);
    }
    (renderer.*end)();
  };

  renderSorted(softDraws, &Renderer::beginSoftTransparentPass, &Renderer::endSoftTransparentPass);
  renderSorted(transparentDraws, &Renderer::beginTransparentPass, &Renderer::endTransparentPass);
  renderer.setFaceCulling(true); // a double-sided material may have turned it off

  // Transparent / Particles pass
  if (systemManager.hasSystem<ParticleSystem>()) {
    systemManager.getSystem<ParticleSystem>().render(systemManager);
  }
}

Renderer &RenderSystem::getRenderer() { return m_renderer; }
