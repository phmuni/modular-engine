#include "system/renderSystem.h"

void RenderSystem::addRenderable(Entity entity) { m_entries.emplace_back(entity); }

void RenderSystem::renderCall(ShaderManager &shaderManager, SystemManager &systemManager, EntityManager &entityManager,
                              ComponentManager &componentManager, ActiveCameraManager &cameraManager) {
  auto &renderer = getRenderer();
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  auto &lightSystem = systemManager.getSystem<LightSystem>();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();

  if (lightSystem.getLights().empty())
    return;

  // ========== SHADOW PASS ==========

  Shader &depthShader = shaderManager.getShader("shadow");

  Entity lightEntity = lightSystem.getLights()[0];
  const auto &light = componentManager.get<LightComponent>(lightEntity);

  glm::mat4 lightProjection = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 20.0f);
  glm::mat4 lightView = glm::lookAt(light.position, light.position + light.direction, glm::vec3(0, 1, 0));
  glm::mat4 lightSpaceMatrix = lightProjection * lightView;

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

  // ========== RENDER PASS ==========

  Shader &shader = shaderManager.getShader("base");
  shader.use();

  if (!cameraManager.hasActiveCamera())
    return;

  Entity cameraEntity = cameraManager.getActiveCamera();
  const auto &cameraComponent = componentManager.get<CameraComponent>(cameraEntity);

  glm::mat4 view = cameraSystem.getViewMatrix(cameraComponent);
  glm::mat4 projection = cameraSystem.getProjMatrix(cameraComponent);
  glm::vec3 viewPos = cameraComponent.position;

  shader.setMat4("view", view);
  shader.setMat4("projection", projection);
  shader.setVec3("viewPos", viewPos);
  shader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
  shader.setTex("shadowMap", renderer.getDepthMap(), 3);

  if (!lightSystem.getLights().empty()) {
    lightSystem.uploadLightsToShader(shader, componentManager);
  } else {
    shader.setInt("numLights", 0);
  }

  for (const auto &entry : m_entries) {
    const auto &transform = componentManager.get<TransformComponent>(entry.entity);
    const auto &model = componentManager.get<ModelComponent>(entry.entity);

    glm::mat4 modelMatrix = transformSystem.calculateModelMatrix(transform);
    glm::mat4 MVP = projection * view * modelMatrix;

    shader.setMat4("MVP", MVP);
    shader.setMat4("model", modelMatrix);

    const auto &material = model.material;
    shader.setTex("material.diffuse", material->getDiffuseMap(), 0);
    shader.setTex("material.specular", material->getSpecularMap(), 1);
    shader.setTex("material.emission", material->getEmissionMap(), 2);
    shader.setFloat("material.shininess", material->getShininess());

    renderer.drawMesh(*model.mesh);
  }
}

Renderer &RenderSystem::getRenderer() { return m_renderer; }
