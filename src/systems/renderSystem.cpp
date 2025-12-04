#include "systems/renderSystem.h"
#include "components/lightComponent.h"
#include "components/modelComponent.h"
#include "foundation/ecs/systemManager.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/shaderSystem.h"
#include "systems/transformSystem.h"
#include <algorithm>

void RenderSystem::addRenderable(Entity entity) { m_entries.emplace_back(entity); }
void RenderSystem::removeRenderable(Entity entity) {
  m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                 [entity](const RenderQueue &entry) { return entry.entity == entity; }),
                  m_entries.end());
}

void RenderSystem::renderCall(SystemManager &systemManager, EntityManager &entityManager,
                              ComponentManager &componentManager) {
  auto &renderer = getRenderer();
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  auto &lightSystem = systemManager.getSystem<LightSystem>();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  auto &shaderSystem = systemManager.getSystem<ShaderSystem>();

  if (cameraSystem.getActiveCamera() == -1)
    return;

  glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
  bool useShadows = false;

  const auto &lights = lightSystem.getLights();

  // Shadow Pass: render depth from light source perspective only for directional lights
  // Depth map stored at 2048x2048 for high-quality PCF sampling
  if (!lights.empty()) {
    Entity lightEntity = lights[0];
    const auto &light = componentManager.get<LightComponent>(lightEntity);

    if (light.type == LightType::Directional) {
      useShadows = true;
      Shader &depthShader = shaderSystem.getShader("shadow");

      glm::mat4 lightProjection = glm::ortho(-20.0f, 20.0f, -20.0f, 20.0f, 1.0f, 50.0f);
      glm::mat4 lightView = glm::lookAt(light.position, light.position + light.direction, glm::vec3(0, 1, 0));
      lightSpaceMatrix = lightProjection * lightView;

      depthShader.use();
      depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

      renderer.beginShadowPass();

      for (const auto &entry : m_entries) {
        const auto &transform = componentManager.get<TransformComponent>(entry.entity);
        const auto &model = componentManager.get<ModelComponent>(entry.entity);

        glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
        depthShader.setMat4("model", modelMatrix);
        renderer.drawMesh(*model.mesh);
      }

      renderer.endShadowPass();
    }
  }

  // Main Render Pass: calculate per-fragment lighting with shadow mapping
  // Normal matrix pre-computed on CPU for efficiency (stored in vertex shader uniform)
  Shader &shader = shaderSystem.getShader("base");
  shader.use();

  Entity cameraEntity = cameraSystem.getActiveCamera();
  if (cameraEntity == -1)
    return;
  const auto &cameraComponent = componentManager.get<CameraComponent>(cameraEntity);

  glm::mat4 view = cameraSystem.getViewMatrix(cameraComponent);
  glm::mat4 projection = cameraSystem.getProjMatrix(cameraComponent);
  glm::vec3 viewPos = cameraComponent.position;

  shader.setMat4("view", view);
  shader.setMat4("projection", projection);
  shader.setVec3("viewPos", viewPos);
  shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
  shader.setInt("useShadows", useShadows ? 1 : 0);
  shader.setTex("shadowMap", renderer.getDepthMap(), 3);

  lightSystem.uploadLightsToShader(shader, componentManager);

  for (const auto &entry : m_entries) {
    const auto &transform = componentManager.get<TransformComponent>(entry.entity);
    const auto &model = componentManager.get<ModelComponent>(entry.entity);

    if (!model.mesh || !model.material)
      continue;

    glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
    glm::mat4 MVP = projection * view * modelMatrix;
    glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(modelMatrix)));

    shader.setMat4("MVP", MVP);
    shader.setMat4("model", modelMatrix);
    shader.setMat3("normalMatrix", normalMatrix);

    const auto &material = *model.material;
    shader.setTex("material.diffuse", material.getDiffuseMap(), 0);
    shader.setTex("material.specular", material.getSpecularMap(), 1);
    shader.setTex("material.emission", material.getEmissionMap(), 2);
    shader.setFloat("material.shininess", material.getShininess());

    renderer.drawMesh(*model.mesh);
  }
}

Renderer &RenderSystem::getRenderer() { return m_renderer; }
const std::vector<RenderSystem::RenderQueue> &RenderSystem::getRenderQueue() const { return m_entries; }
