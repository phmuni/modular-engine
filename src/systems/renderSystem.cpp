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

// Mesh bounds moved to world space; rotation keeps the radius, scale stretches it by its largest axis.
bool isMeshVisible(const Frustum &frustum, const Mesh &mesh, const glm::mat4 &modelMatrix) {
  const glm::vec3 center = glm::vec3(modelMatrix * glm::vec4(mesh.getBoundsCenter(), 1.0f));
  const float maxScale = glm::sqrt(glm::max(glm::max(glm::dot(glm::vec3(modelMatrix[0]), glm::vec3(modelMatrix[0])),
                                                     glm::dot(glm::vec3(modelMatrix[1]), glm::vec3(modelMatrix[1]))),
                                            glm::dot(glm::vec3(modelMatrix[2]), glm::vec3(modelMatrix[2]))));
  return frustum.intersectsSphere(center, mesh.getBoundsRadius() * maxScale);
}

glm::mat3 calculateNormalMatrix(const glm::mat4 &modelMatrix) {
  return glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
}

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
  glm::vec3 viewPos = camera.position;
  const Frustum frustum(projection * view);
  m_culledCount = 0;

  glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
  glm::vec3 shadowLightDir = glm::vec3(0.0f, -1.0f, 0.0f);
  bool useShadows = false;

  auto &state = systemManager.getSystem<StateSystem>();
  bool shadowsEnabled = state.isToggled(Toggle::Shadows);
  const auto &lights = lightSystem.getLights();

  // Shadow pass
  if (shadowsEnabled && !lights.empty()) {
    for (const Entity &lightEntity : lights) {
      const auto &light = componentManager.getOrThrow<Light>(lightEntity);

      if (light.type == LightType::Directional) {
        useShadows = true;
        if (m_shadowShaderHandle == 0)
          break;

        Shader &depthShader = resourceSystem.getShader(m_shadowShaderHandle);

        glm::vec3 sceneCenter = glm::vec3(0.0f);
        float sceneRadius = 30.0f;

        glm::vec3 lightDir = glm::normalize(light.direction);
        glm::vec3 lightPos = sceneCenter - lightDir * sceneRadius * 2.0f;

        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (abs(glm::dot(lightDir, up)) > 0.99f) {
          up = glm::vec3(1.0f, 0.0f, 0.0f);
        }

        float orthoSize = sceneRadius * 1.5f;
        glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, sceneRadius * 4.0f);
        glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, up);
        lightSpaceMatrix = lightProjection * lightView;

        depthShader.use();
        depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

        renderer.beginShadowPass();
        if (depthShader.supportsInstancing()) {
          depthShader.setInt("useInstancing", 1);
          m_instanceItems.clear();
          componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
            if (isHidden(componentManager, entity))
              return;
            const auto &transform = componentManager.getOrThrow<Transform>(entity);
            m_instanceItems.push_back(
                {model.meshHandle, 0, 0, 0, {transformSystem.calculateModelMatrix(transform), {}}});
          });
          drawInstanceRuns([](const InstanceItem &a, const InstanceItem &b) { return a.meshHandle == b.meshHandle; },
                           [&](const InstanceItem &first, int count) {
                             renderer.drawMeshInstanced(resourceSystem.getMesh(first.meshHandle), count);
                           });
        } else {
          componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
            if (isHidden(componentManager, entity))
              return;

            const auto &transform = componentManager.getOrThrow<Transform>(entity);

            const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);

            glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
            depthShader.setMat4("model", modelMatrix);
            renderer.drawMesh(mesh);
          });
        }
        renderer.endShadowPass();
        shadowLightDir = light.direction;

        break;
      }
    }
  }

  // Main pass - grouped by shader
  if (m_batchesDirty) {
    rebuildRenderBatches(componentManager, resourceSystem);
  }

  auto configureShader = [&](Shader &shader) {
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);
    shader.setVec3("viewPos", viewPos);
    shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
    shader.setInt("useShadows", useShadows ? 1 : 0);
    shader.setVec3("shadowLightDir", shadowLightDir);
    shader.setTex("shadowMap", renderer.getDepthMap(), 4);

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
