#pragma once
// A short way to write an EntityDesc in a test (designated initializers are C++20): Desc("box").at(1, 2, 3).body(...)

#include "foundation/core/gameEntity.h"
#include <string>
#include <utility>
#include <vector>

class Desc {
public:
  explicit Desc(std::string model = "") { m_desc.model = std::move(model); }

  Desc &at(float x, float y, float z) {
    m_desc.position = glm::vec3(x, y, z);
    return *this;
  }
  Desc &size(float x, float y, float z) {
    m_desc.scale = glm::vec3(x, y, z);
    return *this;
  }
  Desc &size(float all) { return size(all, all, all); }
  Desc &color(float r, float g, float b) {
    m_desc.color = glm::vec3(r, g, b);
    return *this;
  }
  Desc &body(BodyType type, BodyShape shape = BodyShape::Box, float mass = 0.0f) {
    m_desc.body = type;
    m_desc.shape = shape;
    m_desc.mass = mass;
    return *this;
  }
  Desc &collision() {
    m_desc.collision = true;
    return *this;
  }
  Desc &character() {
    m_desc.character = CharacterController();
    return *this;
  }
  Desc &tags(std::vector<std::string> tags) {
    m_desc.tags = std::move(tags);
    return *this;
  }
  Desc &components(std::vector<std::string> components) {
    m_desc.components = std::move(components);
    return *this;
  }

  operator const EntityDesc &() const { return m_desc; }

private:
  EntityDesc m_desc;
};
