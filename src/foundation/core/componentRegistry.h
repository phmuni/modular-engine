#pragma once
// Describes a game's own component to the engine: its fields. A registered component is saved in scene files, shown
// in the editor's Properties panel with a widget per field, and listed in Add Component.
//
//   engine.registerComponent<Player>("Player")
//       .field("speed", &Player::speed)
//       .field("jumpSpeed", &Player::jumpSpeed);
//
// Field types: float, int, bool, std::string, glm::vec2, glm::vec3 and glm::vec4. A vec3 whose name contains "color"
// is edited with a color picker. The component needs a default constructor to be added from the editor and to be
// loaded from a file; fields missing from a file keep the values that constructor gave them.
//
// Everything here is type-erased and free of JSON, so including it (through engine.h) costs little; the scene
// serializer reads and writes the fields by their kind.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

enum class FieldKind { Float, Int, Bool, String, Vec2, Vec3, Vec4 };

namespace reflection_detail {
template <typename T> struct FieldKindOf {
  static constexpr bool supported = false;
};
#define MODULAR_FIELD_KIND(Type, Kind)                                                                                 \
  template <> struct FieldKindOf<Type> {                                                                              \
    static constexpr bool supported = true;                                                                           \
    static constexpr FieldKind kind = FieldKind::Kind;                                                                \
  };
MODULAR_FIELD_KIND(float, Float)
MODULAR_FIELD_KIND(int, Int)
MODULAR_FIELD_KIND(bool, Bool)
MODULAR_FIELD_KIND(std::string, String)
MODULAR_FIELD_KIND(glm::vec2, Vec2)
MODULAR_FIELD_KIND(glm::vec3, Vec3)
MODULAR_FIELD_KIND(glm::vec4, Vec4)
#undef MODULAR_FIELD_KIND
} // namespace reflection_detail

struct ReflectedField {
  std::string name;
  FieldKind kind;
  // The field inside a component: a float*, int*, bool*, std::string*, glm::vec2*... as `kind` says
  std::function<void *(void *component)> access;
};

struct ReflectedComponent {
  std::string name;
  // The entity's component, or null when it has none
  std::function<void *(ComponentManager &, Entity)> get;
  // Adds a default component (replacing any) and returns it; empty when the type has no default constructor
  std::function<void *(ComponentManager &, Entity)> add;
  std::function<void(ComponentManager &, Entity)> remove;
  std::vector<ReflectedField> fields;
};

template <typename T> class ComponentBuilder {
public:
  explicit ComponentBuilder(ReflectedComponent &component) : m_component(component) {}

  template <typename Member> ComponentBuilder &field(std::string name, Member T::*member) {
    static_assert(reflection_detail::FieldKindOf<Member>::supported,
                  "registerComponent<T>().field(): unsupported member type. Use float, int, bool, std::string, "
                  "glm::vec2, glm::vec3 or glm::vec4.");
    if constexpr (reflection_detail::FieldKindOf<Member>::supported) {
      m_component.fields.push_back(
          {std::move(name), reflection_detail::FieldKindOf<Member>::kind,
           [member](void *component) -> void * { return &(static_cast<T *>(component)->*member); }});
    }
    return *this;
  }

private:
  ReflectedComponent &m_component;
};

// What registerComponent<T> fills in before the fields are added
template <typename T> ReflectedComponent makeReflectedComponent(std::string name) {
  ReflectedComponent component;
  component.name = std::move(name);
  component.get = [](ComponentManager &components, Entity entity) -> void * { return components.tryGet<T>(entity); };
  if constexpr (std::is_default_constructible_v<T>)
    component.add = [](ComponentManager &components, Entity entity) -> void * {
      return &components.addInPlace<T>(entity);
    };
  component.remove = [](ComponentManager &components, Entity entity) { components.removeComponent<T>(entity); };
  return component;
}
