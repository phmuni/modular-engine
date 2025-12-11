#include "systems/renderSystem.h"
#include "components/lightComponent.h"
#include "components/modelComponent.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "rendering/resources/shader.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/resourceSystem.h"
#include "systems/transformSystem.h"
#include <algorithm>
#include <unordered_map>

void RenderSystem::insertRenderable(Entity entity) { m_entries.emplace_back(entity); }

void RenderSystem::removeRenderable(Entity entity) {
  m_entries.erase(std::remove(m_entries.begin(), m_entries.end(), entity), m_entries.end());
}

// renderSystem.cpp - atualizado para usar normal map e ajustar texture units
void RenderSystem::renderCall(SystemManager &systemManager, EntityManager &entityManager,
                              ComponentManager &componentManager) {
  auto &renderer = getRenderer();
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  auto &lightSystem = systemManager.getSystem<LightSystem>();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();

  Entity cameraEntity = cameraSystem.getActiveCamera();
  if (cameraEntity == -1)
    return;

  const auto &cameraComponent = componentManager.get<CameraComponent>(cameraEntity);
  glm::mat4 view = cameraSystem.getViewMatrix(cameraComponent);
  glm::mat4 projection = cameraSystem.getProjMatrix(cameraComponent);
  glm::vec3 viewPos = cameraComponent.position;

  glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
  bool useShadows = false;
  const auto &lights = lightSystem.getLights();

  // Shadow Pass
  if (!lights.empty()) {
    for (const Entity &lightEntity : lights) {
      const auto &light = componentManager.get<LightComponent>(lightEntity);

      if (light.type == LightType::Directional) {
        useShadows = true;
        Shader &depthShader = resourceSystem.getShader(1);

        glm::vec3 sceneCenter = glm::vec3(0.0f);
        float sceneRadius = 30.0f;

        // Normalize direction and position light far from scene
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
        for (const Entity &entity : m_entries) {
          const auto &transform = componentManager.get<TransformComponent>(entity);
          const auto &model = componentManager.get<ModelComponent>(entity);
          const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);

          glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
          depthShader.setMat4("model", modelMatrix);
          renderer.drawMesh(mesh);
        }
        renderer.endShadowPass();

        break;
      }
    }
  }

  // Main Render Pass - Group by shader for efficiency
  // Build map: shaderHandle -> vector of (entity, submeshIndex)
  std::unordered_map<uint32_t, std::vector<std::pair<Entity, size_t>>> renderBatches;

  for (const Entity entity : m_entries) {
    const auto &model = componentManager.get<ModelComponent>(entity);
    const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
    const auto &submeshes = mesh.getSubmeshes();

    for (size_t i = 0; i < submeshes.size(); ++i) {
      const Material &material = resourceSystem.getMaterial(model.materialHandles[i]);
      uint32_t shaderHandle = material.getShaderHandle();
      renderBatches[shaderHandle].emplace_back(entity, i);
    }
  }

  // Render grouped by shader
  for (auto &[shaderHandle, batch] : renderBatches) {
    Shader &shader = resourceSystem.getShader(shaderHandle);
    shader.use();

    // Set per-frame uniforms once per shader
    shader.setMat4("view", view);
    shader.setMat4("projection", projection);
    shader.setVec3("viewPos", viewPos);
    shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
    shader.setInt("useShadows", useShadows ? 1 : 0);

    // shadowMap agora em outra texture unit (ex: 4)
    shader.setTex("shadowMap", renderer.getDepthMap(), 4);

    lightSystem.uploadLightsToShader(shader, componentManager);

    // Render all submeshes using this shader
    for (const auto &[entity, submeshIndex] : batch) {
      const auto &transform = componentManager.get<TransformComponent>(entity);
      const auto &model = componentManager.get<ModelComponent>(entity);
      const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
      const Material &material = resourceSystem.getMaterial(model.materialHandles[submeshIndex]);

      glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
      glm::mat4 MVP = projection * view * modelMatrix;
      glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(modelMatrix)));

      // Set per-object uniforms
      shader.setMat4("MVP", MVP);
      shader.setMat4("model", modelMatrix);
      shader.setMat3("normalMatrix", normalMatrix);

      // Set material properties
      shader.setTex("material.diffuse", material.getDiffuse(), 0);
      shader.setTex("material.specular", material.getSpecular(), 1);
      shader.setTex("material.normal", material.getNormal(), 2);
      shader.setTex("material.emission", material.getEmission(), 3);
      shader.setFloat("material.shininess", material.getShininess());

      renderer.drawSubmesh(mesh, mesh.getSubmeshes()[submeshIndex]);
    }
  }
}

Renderer &RenderSystem::getRenderer() { return m_renderer; }

const std::vector<Entity> &RenderSystem::getRenderQueue() const { return m_entries; }
