#pragma once
// Light component data (directional, point, spot).

#include <glm/glm.hpp>

enum class LightType { Directional, Point, Spot };

struct Light {
  LightType type = LightType::Point;
  glm::vec3 position;
  glm::vec3 direction;
  glm::vec3 color;
  float intensity;
  float ambient;
  float constant;
  float linear;
  float quadratic;
  float cutOff;
  float outerCutOff;

  Light(LightType tp, const glm::vec3 &pos = glm::vec3(0.0f),
                 const glm::vec3 &dir = glm::vec3(0.0f, -1.0f, 0.0f), const glm::vec3 &clr = glm::vec3(1.0f),
                 float in = 1.0f, float amb = 0.2f, float cnst = 1.0f, float lin = 0.09f, float quad = 0.032f,
                 float cut = glm::cos(glm::radians(12.5f)), float outerCut = glm::cos(glm::radians(17.5f)))
      : type(tp), position(pos), direction(dir), color(clr), intensity(in), ambient(amb), constant(cnst), linear(lin),
        quadratic(quad), cutOff(cut), outerCutOff(outerCut) {}
};
