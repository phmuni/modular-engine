
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
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace {
struct TransparentDraw {
  Entity entity;
  size_t submeshIndex;
  float distance;
  uint32_t shaderHandle;
};

constexpr float kSoftOpacityEnter = 0.95f;
constexpr float kSoftOpacityExit = 0.90f;
} // namespace

void RenderSystem::setShadowShaderHandle(uint32_t handle) { m_shadowShaderHandle = handle; }

void RenderSystem::markBatchesDirty() { m_batchesDirty = true; }

void RenderSystem::rebuildRenderBatches(ComponentManager &componentManager, ResourceSystem &resourceSystem) {
  m_renderBatches.clear();

  componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
    if (componentManager.containsComponent<Hidden>(entity))
      return;

    if (!componentManager.containsComponent<Transform>(entity))
      return;

    if (model.opacity < kSoftOpacityEnter)
      return;

    const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
    const auto &submeshes = mesh.getSubmeshes();

    for (size_t i = 0; i < submeshes.size(); ++i) {
      const Material &material = resourceSystem.getMaterial(model.materialHandles[i]);
      m_renderBatches[material.getShaderHandle()].emplace_back(entity, i);
    }
  });

  m_batchesDirty = false;
}

void RenderSystem::renderPipeline(SystemManager &systemManager, EntityManager & /*entityManager*/,
                                  ComponentManager &componentManager) {
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
        int shadowCount = 0;
        componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
          if (componentManager.containsComponent<Hidden>(entity))
            return;

          if (!componentManager.containsComponent<Transform>(entity))
            return;

          const auto &transform = componentManager.getOrThrow<Transform>(entity);

          const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);

          glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
          depthShader.setMat4("model", modelMatrix);
          renderer.drawMesh(mesh);
          shadowCount++;
        });
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

  for (auto &[shaderHandle, batch] : m_renderBatches) {
    Shader &shader = resourceSystem.getShader(shaderHandle);
    shader.use();
    configureShader(shader);

    for (const auto &[entity, submeshIndex] : batch) {
      if (!componentManager.containsComponent<Transform>(entity) || !componentManager.containsComponent<Model>(entity))
        continue;

      const auto &transform = componentManager.getOrThrow<Transform>(entity);
      const auto &model = componentManager.getOrThrow<Model>(entity);
      const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
      const Material &material = resourceSystem.getMaterial(model.materialHandles[submeshIndex]);

      glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
      glm::mat4 MVP = projection * view * modelMatrix;
      glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(modelMatrix)));

      shader.setMat4("MVP", MVP);
      shader.setMat4("model", modelMatrix);
      shader.setMat3("normalMatrix", normalMatrix);

      shader.setTex("material.diffuse", material.getDiffuse(), 0);
      shader.setTex("material.specular", material.getSpecular(), 1);
      // material.normal disabled until TBN is implemented
      shader.setTex("material.emission", material.getEmission(), 2);
      shader.setFloat("material.shininess", material.getShininess());
      shader.setVec3("material.emissionColor", material.getEmissionColor());
      shader.setFloat("material.emissionStrength", material.getEmissionStrength());
      shader.setInt("useSolidDiffuseColor", material.hasDiffuseTexture() ? 0 : 1);
      shader.setVec3("solidDiffuseColor", material.getDiffuseColor());

      shader.setFloat("opacity", 1.0f);
      renderer.drawSubmesh(mesh, mesh.getSubmeshes()[submeshIndex]);
    }
  }

  std::vector<TransparentDraw> softDraws;
  std::vector<TransparentDraw> transparentDraws;

  componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
    if (componentManager.containsComponent<Hidden>(entity))
      return;

    if (!componentManager.containsComponent<Transform>(entity))
      return;

    const auto &transform = componentManager.getOrThrow<Transform>(entity);
    const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
    const auto &submeshes = mesh.getSubmeshes();
    float distance = glm::length(camera.position - transform.position);

    if (model.opacity < kSoftOpacityEnter && model.opacity >= kSoftOpacityExit) {
      for (size_t i = 0; i < submeshes.size(); ++i) {
        const Material &material = resourceSystem.getMaterial(model.materialHandles[i]);
        softDraws.push_back({entity, i, distance, material.getShaderHandle()});
      }
      return;
    }

    if (model.opacity < kSoftOpacityExit) {
      for (size_t i = 0; i < submeshes.size(); ++i) {
        const Material &material = resourceSystem.getMaterial(model.materialHandles[i]);
        transparentDraws.push_back({entity, i, distance, material.getShaderHandle()});
      }
      return;
    }
  });

  std::sort(transparentDraws.begin(), transparentDraws.end(),
            [](const TransparentDraw &a, const TransparentDraw &b) { return a.distance > b.distance; });

  if (!softDraws.empty()) {

    std::sort(softDraws.begin(), softDraws.end(),
              [](const TransparentDraw &a, const TransparentDraw &b) { return a.distance > b.distance; });

    renderer.beginSoftTransparentPass();
    for (const auto &draw : softDraws) {
      Shader &shader = resourceSystem.getShader(draw.shaderHandle);
      shader.use();
      configureShader(shader);

      if (!componentManager.containsComponent<Transform>(draw.entity) ||
          !componentManager.containsComponent<Model>(draw.entity))
        continue;

      const auto &transform = componentManager.getOrThrow<Transform>(draw.entity);
      const auto &model = componentManager.getOrThrow<Model>(draw.entity);
      const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
      const Material &material = resourceSystem.getMaterial(model.materialHandles[draw.submeshIndex]);

      glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
      glm::mat4 MVP = projection * view * modelMatrix;
      glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(modelMatrix)));

      shader.setMat4("MVP", MVP);
      shader.setMat4("model", modelMatrix);
      shader.setMat3("normalMatrix", normalMatrix);

      shader.setTex("material.diffuse", material.getDiffuse(), 0);
      shader.setTex("material.specular", material.getSpecular(), 1);
      shader.setTex("material.emission", material.getEmission(), 2);
      shader.setFloat("material.shininess", material.getShininess());
      shader.setVec3("material.emissionColor", material.getEmissionColor());
      shader.setFloat("material.emissionStrength", material.getEmissionStrength());
      shader.setInt("useSolidDiffuseColor", material.hasDiffuseTexture() ? 0 : 1);
      shader.setVec3("solidDiffuseColor", material.getDiffuseColor());
      shader.setFloat("opacity", model.opacity);

      renderer.drawSubmesh(mesh, mesh.getSubmeshes()[draw.submeshIndex]);
    }
    renderer.endSoftTransparentPass();
  }

  if (!transparentDraws.empty()) {
    renderer.beginTransparentPass();

    for (const auto &draw : transparentDraws) {
      Shader &shader = resourceSystem.getShader(draw.shaderHandle);
      shader.use();
      configureShader(shader);

      if (!componentManager.containsComponent<Transform>(draw.entity) ||
          !componentManager.containsComponent<Model>(draw.entity))
        continue;

      const auto &transform = componentManager.getOrThrow<Transform>(draw.entity);
      const auto &model = componentManager.getOrThrow<Model>(draw.entity);
      const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
      const Material &material = resourceSystem.getMaterial(model.materialHandles[draw.submeshIndex]);

      glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
      glm::mat4 MVP = projection * view * modelMatrix;
      glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(modelMatrix)));

      shader.setMat4("MVP", MVP);
      shader.setMat4("model", modelMatrix);
      shader.setMat3("normalMatrix", normalMatrix);

      shader.setTex("material.diffuse", material.getDiffuse(), 0);
      shader.setTex("material.specular", material.getSpecular(), 1);
      shader.setTex("material.emission", material.getEmission(), 2);
      shader.setFloat("material.shininess", material.getShininess());
      shader.setVec3("material.emissionColor", material.getEmissionColor());
      shader.setFloat("material.emissionStrength", material.getEmissionStrength());
      shader.setInt("useSolidDiffuseColor", material.hasDiffuseTexture() ? 0 : 1);
      shader.setVec3("solidDiffuseColor", material.getDiffuseColor());
      shader.setFloat("opacity", model.opacity);

      renderer.drawSubmesh(mesh, mesh.getSubmeshes()[draw.submeshIndex]);
    }

    renderer.endTransparentPass();
  }

  // Transparent / Particles pass
  if (systemManager.hasSystem<ParticleSystem>()) {
    systemManager.getSystem<ParticleSystem>().render(systemManager);
  }
}

Renderer &RenderSystem::getRenderer() { return m_renderer; }
