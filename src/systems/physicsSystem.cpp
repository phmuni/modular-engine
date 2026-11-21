// Jolt-backed physics: bodies and characters kept in step with the ECS.

#include "systems/physicsSystem.h"

#include "components/characterController.h"
#include "components/model.h"
#include "components/rigidBody.h"
#include "components/transform.h"
#include "foundation/core/collisionLayers.h"
#include "foundation/core/logger.h"
#include "rendering/resources/mesh.h"
#include "systems/resourceSystem.h"
#include "systems/transformSystem.h"

// Jolt.h comes before any other Jolt header
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {
constexpr float kStep = 1.0f / 60.0f;
constexpr int kMaxStepsPerUpdate = 4; // after a long hitch, slow down rather than spiral

// Jolt's object layers are a collision layer (CollisionLayers) times two: the low bit says whether the body moves. The
// broadphase keeps only that split, so what never moves is not tested against itself however many layers there are,
// and the matrix is checked per pair of object layers.
namespace Layers {
constexpr JPH::ObjectLayer objectLayer(int layer, bool moving) {
  return static_cast<JPH::ObjectLayer>(layer * 2 + (moving ? 1 : 0));
}
constexpr bool isMoving(JPH::ObjectLayer layer) { return (layer & 1) != 0; }
constexpr int collisionLayer(JPH::ObjectLayer layer) { return layer >> 1; }
} // namespace Layers
namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NonMoving(0);
constexpr JPH::BroadPhaseLayer Moving(1);
constexpr unsigned Count = 2;
} // namespace BroadPhaseLayers

class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter {
public:
  explicit ObjectLayerPairs(const CollisionLayers &layers) : m_layers(layers) {}
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return (Layers::isMoving(a) || Layers::isMoving(b)) &&
           m_layers.collide(Layers::collisionLayer(a), Layers::collisionLayer(b));
  }

private:
  const CollisionLayers &m_layers;
};

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
  unsigned GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return Layers::isMoving(layer) ? BroadPhaseLayers::Moving : BroadPhaseLayers::NonMoving;
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
  }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
    return Layers::isMoving(layer) || broadPhaseLayer == BroadPhaseLayers::Moving;
  }
};

// Lets a query see only the collision layers in a mask
class LayerMaskFilter final : public JPH::ObjectLayerFilter {
public:
  explicit LayerMaskFilter(uint32_t mask) : m_mask(mask) {}
  bool ShouldCollide(JPH::ObjectLayer layer) const override {
    return ((m_mask >> Layers::collisionLayer(layer)) & 1u) != 0;
  }

private:
  uint32_t m_mask;
};

// Collects new contacts; Jolt calls it from its worker threads
class ContactCollector final : public JPH::ContactListener {
public:
  void OnContactAdded(const JPH::Body &a, const JPH::Body &b, const JPH::ContactManifold &manifold,
                      JPH::ContactSettings &) override {
    const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
    const JPH::Vec3 normal = manifold.mWorldSpaceNormal;
    std::lock_guard<std::mutex> lock(mutex);
    contacts.push_back({static_cast<Entity>(a.GetUserData()), static_cast<Entity>(b.GetUserData()),
                        glm::vec3(point.GetX(), point.GetY(), point.GetZ()),
                        glm::vec3(normal.GetX(), normal.GetY(), normal.GetZ())});
  }
  std::mutex mutex;
  std::vector<PhysicsContact> contacts;
};

JPH::Vec3 toJolt(const glm::vec3 &v) { return JPH::Vec3(v.x, v.y, v.z); }
glm::vec3 toGlm(const JPH::Vec3 &v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
JPH::Quat toJolt(const glm::quat &q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
glm::quat toGlm(const JPH::Quat &q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

// The same order as TransformSystem::calculateModelMatrix: Y, then X, then Z
glm::quat rotationOf(const Transform &transform) {
  const glm::vec3 radians = glm::radians(transform.rotation);
  return glm::angleAxis(radians.y, glm::vec3(0, 1, 0)) * glm::angleAxis(radians.x, glm::vec3(1, 0, 0)) *
         glm::angleAxis(radians.z, glm::vec3(0, 0, 1));
}

void trace(const char *format, ...) {
  char message[1024];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  LOG_I("[Jolt] %s", message);
}

#ifdef JPH_ENABLE_ASSERTS
bool assertFailed(const char *expression, const char *message, const char *file, unsigned line) {
  LOG_E("[Jolt] %s:%u: (%s) %s", file, line, expression, message ? message : "");
  return false; // carry on rather than break into a debugger
}
#endif

// Jolt's process-wide setup, done once however many engines come and go
void initializeJolt() {
  static bool done = [] {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = trace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = assertFailed;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    return true;
  }();
  (void)done;
}
} // namespace

struct PhysicsSystem::Impl {
  ComponentManager &componentManager;
  ResourceSystem &resourceSystem;
  TransformSystem &transformSystem;

  const CollisionLayers &layers;
  uint32_t layersVersion;
  ObjectLayerPairs objectLayerPairs;
  BroadPhaseLayerMap broadPhaseLayers;
  ObjectVsBroadPhase objectVsBroadPhase;
  ContactCollector contactCollector;
  std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
  std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
  std::unique_ptr<JPH::PhysicsSystem> system;

  // What each entity's body was built from, to rebuild it when that changes, and its last written Transform, to
  // tell a move by game code or the editor (handed to the body) from the simulation's own
  struct BodyRecord {
    JPH::BodyID id;
    RigidBody settings;
    glm::vec3 scale;
    uint32_t meshHandle;
    Transform written;
  };
  std::unordered_map<Entity, BodyRecord> bodies;

  // Velocity, impulse and force set on an entity whose body Jolt does not have yet (created this frame): applied when
  // sync builds it. Empty almost always, so it costs nothing per frame.
  struct PendingMotion {
    std::optional<glm::vec3> linear, angular;
    glm::vec3 impulse{0.0f}, force{0.0f};
  };
  std::unordered_map<Entity, PendingMotion> pending;

  // The entity's pending motion, if it is a dynamic body that sync has yet to build
  PendingMotion *pendingFor(Entity entity) {
    if (bodies.count(entity))
      return nullptr;
    const RigidBody *body = componentManager.tryGet<RigidBody>(entity);
    if (!body || body->type != BodyType::Dynamic || componentManager.has<CharacterController>(entity))
      return nullptr;
    return &pending[entity];
  }

  struct CharacterRecord {
    JPH::Ref<JPH::CharacterVirtual> character;
    CharacterController settings; // the shape part, to rebuild it
    Transform written;
    std::string layerName; // what layerIndex was resolved from, and when
    uint32_t layerVersion = 0;
    int layerIndex = 0;
  };
  std::unordered_map<Entity, CharacterRecord> characters;

  std::vector<PhysicsContact> contacts; // from the last update
  float accumulator = 0.0f;

  Impl(ComponentManager &cm, ResourceSystem &rs, TransformSystem &ts, const CollisionLayers &collisionLayers)
      : componentManager(cm), resourceSystem(rs), transformSystem(ts), layers(collisionLayers),
        layersVersion(collisionLayers.version()), objectLayerPairs(collisionLayers) {
    initializeJolt();
    tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    const int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    system = std::make_unique<JPH::PhysicsSystem>();
    system->Init(65536, 0, 65536, 20480, broadPhaseLayers, objectVsBroadPhase, objectLayerPairs);
    system->SetContactListener(&contactCollector);
  }

  ~Impl() {
    removeAll();
    system.reset();
  }

  void removeAll() {
    JPH::BodyInterface &bodyInterface = system->GetBodyInterface();
    for (auto &[entity, record] : bodies) {
      bodyInterface.RemoveBody(record.id);
      bodyInterface.DestroyBody(record.id);
    }
    bodies.clear();
    characters.clear();
    pending.clear();
  }

  // The body's shape in world units: the component's size (or the model's bounds) times the entity's scale
  JPH::ShapeRefC buildShape(Entity entity, const RigidBody &body, const glm::vec3 &scale) {
    const Model *model = componentManager.tryGet<Model>(entity);
    const Mesh *mesh = model ? &resourceSystem.getMesh(model->meshHandle) : nullptr;
    const bool fromModel = body.size == glm::vec3(0.0f) && mesh;
    const glm::vec3 low = fromModel ? mesh->getBoundsMin() : -body.size * 0.5f;
    const glm::vec3 high = fromModel ? mesh->getBoundsMax() : body.size * 0.5f;
    const glm::vec3 half = glm::max((high - low) * 0.5f * glm::abs(scale), glm::vec3(0.01f));
    const glm::vec3 center = (low + high) * 0.5f * scale;

    BodyShape shape = body.shape;
    if ((shape == BodyShape::ConvexHull || shape == BodyShape::Mesh) && !mesh) {
      LOG_W("[Physics] Entity %d: a %s shape needs a Model; using a box", entity,
            shape == BodyShape::Mesh ? "Mesh" : "ConvexHull");
      shape = BodyShape::Box;
    }
    if (shape == BodyShape::Mesh && body.type == BodyType::Dynamic)
      shape = BodyShape::ConvexHull; // Jolt simulates convex shapes only; a mesh cannot fall

    JPH::ShapeSettings::ShapeResult result;
    bool centered = true; // the shapes made from the model's vertices are already where the model is
    switch (shape) {
    case BodyShape::Box: {
      const float convexRadius = std::min(0.05f, std::min(half.x, std::min(half.y, half.z)) * 0.5f);
      result = JPH::BoxShapeSettings(toJolt(half), convexRadius).Create();
      centered = false;
      break;
    }
    case BodyShape::Sphere:
      result = JPH::SphereShapeSettings(std::max(half.x, std::max(half.y, half.z))).Create();
      centered = false;
      break;
    case BodyShape::Capsule: {
      const float radius = std::max(half.x, half.z);
      result = JPH::CapsuleShapeSettings(std::max(half.y - radius, 0.01f), radius).Create();
      centered = false;
      break;
    }
    case BodyShape::ConvexHull: {
      JPH::Array<JPH::Vec3> points;
      for (const glm::vec3 &vertex : mesh->getCollisionVertices())
        points.push_back(toJolt(vertex * scale));
      result = JPH::ConvexHullShapeSettings(points).Create();
      break;
    }
    case BodyShape::Mesh: {
      JPH::VertexList vertices;
      for (const glm::vec3 &vertex : mesh->getCollisionVertices())
        vertices.push_back(JPH::Float3(vertex.x * scale.x, vertex.y * scale.y, vertex.z * scale.z));
      JPH::IndexedTriangleList triangles;
      const auto &indices = mesh->getCollisionIndices();
      for (size_t i = 0; i + 2 < indices.size(); i += 3)
        triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));
      result = JPH::MeshShapeSettings(vertices, triangles).Create();
      break;
    }
    }
    if (result.HasError()) {
      LOG_W("[Physics] Entity %d: could not build its shape (%s); using a box", entity, result.GetError().c_str());
      result = JPH::BoxShapeSettings(toJolt(half)).Create();
      centered = false;
    }
    if (!centered && glm::length(center) > 1e-4f)
      result = JPH::RotatedTranslatedShapeSettings(toJolt(center), JPH::Quat::sIdentity(), result.Get()).Create();
    return result.Get();
  }

  void createBody(Entity entity, const RigidBody &body, const Transform &transform) {
    JPH::ShapeRefC shape = buildShape(entity, body, transform.scale);
    const JPH::EMotionType motion = body.type == BodyType::Static    ? JPH::EMotionType::Static
                                    : body.type == BodyType::Dynamic ? JPH::EMotionType::Dynamic
                                                                     : JPH::EMotionType::Kinematic;
    JPH::BodyCreationSettings settings(shape, JPH::RVec3(toJolt(transform.position)), toJolt(rotationOf(transform)),
                                       motion, Layers::objectLayer(layers.resolve(body.layer), body.type != BodyType::Static));
    settings.mUserData = static_cast<JPH::uint64>(entity);
    settings.mFriction = body.friction;
    settings.mRestitution = body.restitution;
    settings.mLinearDamping = body.linearDamping;
    settings.mAngularDamping = body.angularDamping;
    settings.mGravityFactor = body.gravityScale;
    settings.mIsSensor = body.sensor;
    if (body.lockRotation)
      settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
                              JPH::EAllowedDOFs::TranslationZ;
    if (body.mass > 0.0f) {
      settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      settings.mMassPropertiesOverride.mMass = body.mass;
    }
    const JPH::BodyID id = system->GetBodyInterface().CreateAndAddBody(
        settings, body.type == BodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    if (id.IsInvalid()) {
      LOG_W("[Physics] Entity %d: no room for another body", entity);
      return;
    }
    const Model *model = componentManager.tryGet<Model>(entity);
    bodies[entity] = BodyRecord{id, body, transform.scale, model ? model->meshHandle : UINT32_MAX, transform};
    if (pending.empty())
      return;
    if (auto it = pending.find(entity); it != pending.end()) {
      JPH::BodyInterface &bodyInterface = system->GetBodyInterface();
      const PendingMotion &motion = it->second;
      if (body.type == BodyType::Dynamic) {
        if (motion.linear)
          bodyInterface.SetLinearVelocity(id, toJolt(*motion.linear));
        if (motion.angular)
          bodyInterface.SetAngularVelocity(id, toJolt(*motion.angular));
        bodyInterface.AddImpulse(id, toJolt(motion.impulse));
        bodyInterface.AddForce(id, toJolt(motion.force));
      }
      pending.erase(it);
    }
  }

  void createCharacter(Entity entity, const CharacterController &controller, const Transform &transform) {
    const float radius = std::max(controller.radius, 0.05f);
    const float halfCylinder = std::max(controller.height * 0.5f - radius, 0.01f);
    JPH::CharacterVirtualSettings settings;
    settings.mShape = new JPH::CapsuleShape(halfCylinder, radius);
    settings.mMaxSlopeAngle = glm::radians(controller.maxSlope);
    // Contacts below the lower sphere's middle are ground
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), halfCylinder);
    settings.mMass = 70.0f;
    settings.mMaxStrength = 200.0f; // how hard it pushes dynamic bodies
    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
        &settings, JPH::RVec3(toJolt(transform.position)), JPH::Quat::sIdentity(), static_cast<JPH::uint64>(entity),
        system.get());
    characters[entity] = CharacterRecord{character, controller, transform, controller.layer, layers.version(),
                                         layers.resolve(controller.layer)};
  }

  // Bodies and characters for the components there are now, with Transforms moved from outside handed over
  void sync(float stepTime) {
    JPH::BodyInterface &bodyInterface = system->GetBodyInterface();
    // Layers renamed, removed or reconfigured since the last sync: numbers are looked up again below
    const bool layersChanged = layersVersion != layers.version();
    layersVersion = layers.version();
    bool layerMoved = false; // a body changed layer: what rested on it or beside it has to look again

    // Gone: the entity, its component, or (for a body) it became a character
    for (auto it = bodies.begin(); it != bodies.end();) {
      const RigidBody *body = componentManager.tryGet<RigidBody>(it->first);
      if (!body || !componentManager.has<Transform>(it->first) || componentManager.has<CharacterController>(it->first)) {
        bodyInterface.RemoveBody(it->second.id);
        bodyInterface.DestroyBody(it->second.id);
        it = bodies.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = characters.begin(); it != characters.end();) {
      if (!componentManager.has<CharacterController>(it->first) || !componentManager.has<Transform>(it->first))
        it = characters.erase(it);
      else
        ++it;
    }

    componentManager.forEachComponent<RigidBody>([&](Entity entity, RigidBody &body) {
      const Transform *transform = componentManager.tryGet<Transform>(entity);
      if (!transform || componentManager.has<CharacterController>(entity))
        return;
      auto it = bodies.find(entity);
      const Model *model = componentManager.tryGet<Model>(entity);
      const uint32_t meshHandle = model ? model->meshHandle : UINT32_MAX;
      if (it != bodies.end() && (it->second.settings != body || it->second.scale != transform->scale ||
                                 it->second.meshHandle != meshHandle)) {
        // Changed: built again, keeping the motion a dynamic body had (a hit, a push) unless game code set another
        if (it->second.settings.type == BodyType::Dynamic && body.type == BodyType::Dynamic) {
          PendingMotion &motion = pending[entity];
          if (!motion.linear)
            motion.linear = toGlm(bodyInterface.GetLinearVelocity(it->second.id));
          if (!motion.angular && !body.lockRotation)
            motion.angular = toGlm(bodyInterface.GetAngularVelocity(it->second.id));
        }
        bodyInterface.RemoveBody(it->second.id);
        bodyInterface.DestroyBody(it->second.id);
        bodies.erase(it);
        it = bodies.end();
      }
      if (it == bodies.end()) {
        createBody(entity, body, *transform);
        return;
      }
      BodyRecord &record = it->second;
      if (layersChanged || record.settings.layer != body.layer) {
        bodyInterface.SetObjectLayer(record.id, Layers::objectLayer(layers.resolve(body.layer), body.type != BodyType::Static));
        record.settings.layer = body.layer;
        layerMoved = true;
      }
      const JPH::RVec3 position(toJolt(transform->position));
      const JPH::Quat rotation = toJolt(rotationOf(*transform));
      if (body.type == BodyType::Kinematic) {
        // Kinematic bodies are driven by their Transform, smoothly, so they push what they meet
        bodyInterface.MoveKinematic(record.id, position, rotation, stepTime);
        record.written = *transform;
      } else if (*transform != record.written) {
        // Moved by game code or the editor: the body jumps there
        bodyInterface.SetPositionAndRotation(record.id, position, rotation,
                                             body.type == BodyType::Static ? JPH::EActivation::DontActivate
                                                                           : JPH::EActivation::Activate);
        record.written = *transform;
      }
    });
    pending.clear(); // for entities that never got a body (destroyed, or no longer dynamic)
    // Sleeping bodies do not notice that what they rest on stopped colliding with them
    if (layerMoved)
      for (const auto &[entity, record] : bodies)
        if (record.settings.type != BodyType::Static)
          bodyInterface.ActivateBody(record.id);

    componentManager.forEachComponent<CharacterController>([&](Entity entity, CharacterController &controller) {
      const Transform *transform = componentManager.tryGet<Transform>(entity);
      if (!transform)
        return;
      auto it = characters.find(entity);
      if (it != characters.end() &&
          (it->second.settings.radius != controller.radius || it->second.settings.height != controller.height ||
           it->second.settings.maxSlope != controller.maxSlope)) {
        characters.erase(it); // its shape changed: built again
        it = characters.end();
      }
      if (it == characters.end()) {
        createCharacter(entity, controller, *transform);
        return;
      }
      if (*transform != it->second.written) {
        it->second.character->SetPosition(JPH::RVec3(toJolt(transform->position)));
        it->second.written = *transform;
      }
    });
  }

  void updateCharacters(float stepTime) {
    const JPH::Vec3 gravity = system->GetGravity();
    for (auto &[entity, record] : characters) {
      CharacterController &controller = componentManager.get<CharacterController>(entity);
      JPH::CharacterVirtual &character = *record.character;
      if (record.layerName != controller.layer || record.layerVersion != layers.version()) {
        record.layerName = controller.layer;
        record.layerVersion = layers.version();
        record.layerIndex = layers.resolve(controller.layer);
      }
      const JPH::ObjectLayer objectLayer = Layers::objectLayer(record.layerIndex, true);
      character.UpdateGroundVelocity();
      const bool onGround = character.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;

      // On the ground it moves with the ground (a moving platform) and jumps from it; in the air it keeps its
      // vertical speed. Gravity pulls either way, and it goes where the game asks horizontally.
      JPH::Vec3 velocity = onGround ? character.GetGroundVelocity()
                                    : JPH::Vec3(0.0f, character.GetLinearVelocity().GetY(), 0.0f);
      if (onGround && controller.jump > 0.0f)
        velocity += JPH::Vec3(0.0f, controller.jump, 0.0f);
      controller.jump = 0.0f;
      velocity += gravity * controller.gravityScale * stepTime;
      velocity += JPH::Vec3(controller.move.x, 0.0f, controller.move.z);
      character.SetLinearVelocity(velocity);

      JPH::CharacterVirtual::ExtendedUpdateSettings update;
      update.mWalkStairsStepUp = JPH::Vec3(0.0f, controller.stepHeight, 0.0f);
      character.ExtendedUpdate(stepTime, gravity * controller.gravityScale, update,
                               system->GetDefaultBroadPhaseLayerFilter(objectLayer),
                               system->GetDefaultLayerFilter(objectLayer), {}, {}, *tempAllocator);
    }
  }

  void writeBack() {
    JPH::BodyInterface &bodyInterface = system->GetBodyInterface();
    for (auto &[entity, record] : bodies) {
      if (record.settings.type != BodyType::Dynamic)
        continue;
      Transform &transform = componentManager.get<Transform>(entity);
      JPH::RVec3 position;
      JPH::Quat rotation;
      bodyInterface.GetPositionAndRotation(record.id, position, rotation);
      // The rotation back to the Transform's degrees, through the same Y, X, Z decomposition as the editor's
      const Transform placed =
          TransformSystem::decompose(glm::translate(glm::mat4(1.0f), toGlm(JPH::Vec3(position))) *
                                     glm::mat4_cast(toGlm(rotation)));
      transform.position = placed.position;
      transform.rotation = placed.rotation;
      record.written = transform;
    }
    for (auto &[entity, record] : characters) {
      Transform &transform = componentManager.get<Transform>(entity);
      CharacterController &controller = componentManager.get<CharacterController>(entity);
      transform.position = toGlm(JPH::Vec3(record.character->GetPosition()));
      controller.onGround = record.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
      controller.velocity = toGlm(record.character->GetLinearVelocity());
      record.written = transform;
    }
  }

  const BodyRecord *dynamicBody(Entity entity) const {
    auto it = bodies.find(entity);
    return it != bodies.end() && it->second.settings.type == BodyType::Dynamic ? &it->second : nullptr;
  }
};

PhysicsSystem::PhysicsSystem(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                             TransformSystem &transformSystem, const CollisionLayers &layers)
    : m_impl(std::make_unique<Impl>(componentManager, resourceSystem, transformSystem, layers)) {}

PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::update(float deltaTime) {
  Impl &impl = *m_impl;
  impl.contacts.clear();
  if (deltaTime <= 0.0f) {
    impl.sync(kStep); // paused: bodies still follow edits, nothing simulates
    return;
  }
  impl.accumulator = std::min(impl.accumulator + deltaTime, kStep * kMaxStepsPerUpdate);
  while (impl.accumulator >= kStep) {
    impl.accumulator -= kStep;
    impl.sync(kStep);
    impl.updateCharacters(kStep);
    impl.system->Update(kStep, 1, impl.tempAllocator.get(), impl.jobSystem.get());
    impl.writeBack();
  }
  std::lock_guard<std::mutex> lock(impl.contactCollector.mutex);
  impl.contacts.swap(impl.contactCollector.contacts);
  impl.contactCollector.contacts.clear();
}

void PhysicsSystem::syncBodies() { m_impl->sync(kStep); }

void PhysicsSystem::reset() {
  m_impl->removeAll();
  m_impl->accumulator = 0.0f;
  m_impl->contacts.clear();
}

glm::vec3 PhysicsSystem::getGravity() const { return toGlm(m_impl->system->GetGravity()); }
void PhysicsSystem::setGravity(const glm::vec3 &gravity) { m_impl->system->SetGravity(toJolt(gravity)); }

glm::vec3 PhysicsSystem::getLinearVelocity(Entity entity) const {
  if (const auto *record = m_impl->dynamicBody(entity))
    return toGlm(m_impl->system->GetBodyInterface().GetLinearVelocity(record->id));
  const auto it = m_impl->pending.find(entity);
  return it != m_impl->pending.end() && it->second.linear ? *it->second.linear : glm::vec3(0.0f);
}

void PhysicsSystem::setLinearVelocity(Entity entity, const glm::vec3 &velocity) {
  if (const auto *record = m_impl->dynamicBody(entity)) {
    m_impl->system->GetBodyInterface().SetLinearVelocity(record->id, toJolt(velocity));
  } else if (auto *pending = m_impl->pendingFor(entity)) {
    pending->linear = velocity;
    pending->impulse = glm::vec3(0.0f); // the velocity replaces what an earlier impulse would have added
  }
}

glm::vec3 PhysicsSystem::getAngularVelocity(Entity entity) const {
  if (const auto *record = m_impl->dynamicBody(entity))
    return toGlm(m_impl->system->GetBodyInterface().GetAngularVelocity(record->id));
  const auto it = m_impl->pending.find(entity);
  return it != m_impl->pending.end() && it->second.angular ? *it->second.angular : glm::vec3(0.0f);
}

void PhysicsSystem::setAngularVelocity(Entity entity, const glm::vec3 &velocity) {
  if (const auto *record = m_impl->dynamicBody(entity))
    m_impl->system->GetBodyInterface().SetAngularVelocity(record->id, toJolt(velocity));
  else if (auto *pending = m_impl->pendingFor(entity))
    pending->angular = velocity;
}

void PhysicsSystem::addImpulse(Entity entity, const glm::vec3 &impulse) {
  if (const auto *record = m_impl->dynamicBody(entity))
    m_impl->system->GetBodyInterface().AddImpulse(record->id, toJolt(impulse));
  else if (auto *pending = m_impl->pendingFor(entity))
    pending->impulse += impulse;
}

void PhysicsSystem::addForce(Entity entity, const glm::vec3 &force) {
  if (const auto *record = m_impl->dynamicBody(entity))
    m_impl->system->GetBodyInterface().AddForce(record->id, toJolt(force));
  else if (auto *pending = m_impl->pendingFor(entity))
    pending->force += force;
}

std::optional<RayHit> PhysicsSystem::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                             const std::function<bool(Entity)> &accept, uint32_t layerMask) const {
  if (glm::length(direction) <= 0.0f || maxDistance <= 0.0f)
    return std::nullopt;
  const glm::vec3 dir = glm::normalize(direction);
  const JPH::RRayCast ray(JPH::RVec3(toJolt(origin)), toJolt(dir * maxDistance));

  // Skips bodies `accept` turns down
  class Filter final : public JPH::BodyFilter {
  public:
    explicit Filter(const std::function<bool(Entity)> &accept) : m_accept(accept) {}
    bool ShouldCollideLocked(const JPH::Body &body) const override {
      return !m_accept || m_accept(static_cast<Entity>(body.GetUserData()));
    }

  private:
    const std::function<bool(Entity)> &m_accept;
  } filter(accept);

  JPH::RayCastResult hit;
  const LayerMaskFilter layerFilter(layerMask);
  if (!m_impl->system->GetNarrowPhaseQuery().CastRay(ray, hit, {}, layerFilter, filter))
    return std::nullopt;
  JPH::BodyLockRead lock(m_impl->system->GetBodyLockInterface(), hit.mBodyID);
  if (!lock.Succeeded())
    return std::nullopt;
  const JPH::Body &body = lock.GetBody();
  const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
  return RayHit{static_cast<Entity>(body.GetUserData()), hit.mFraction * maxDistance, toGlm(JPH::Vec3(point)),
                toGlm(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point))};
}

bool PhysicsSystem::isCapsuleBlocked(const glm::vec3 &base, float height, float radius) const {
  radius = std::max(radius, 0.01f);
  const float halfCylinder = std::max(height * 0.5f - radius, 0.01f);

  // Only what stays put and blocks
  class Filter final : public JPH::BodyFilter {
  public:
    bool ShouldCollideLocked(const JPH::Body &body) const override {
      return body.GetMotionType() != JPH::EMotionType::Dynamic && !body.IsSensor();
    }
  } filter;

  const JPH::CapsuleShape shape(halfCylinder, radius);
  const glm::vec3 middle = base + glm::vec3(0.0f, halfCylinder + radius, 0.0f);
  JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
  m_impl->system->GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sReplicate(1.0f),
                                                     JPH::RMat44::sTranslation(JPH::RVec3(toJolt(middle))),
                                                     JPH::CollideShapeSettings(), JPH::RVec3::sZero(), collector, {}, {},
                                                     filter);
  return collector.HadHit();
}

bool PhysicsSystem::getStaticBounds(glm::vec3 &min, glm::vec3 &max) const {
  JPH::AABox bounds;
  const JPH::BodyInterface &bodyInterface = m_impl->system->GetBodyInterface();
  for (const auto &[entity, record] : m_impl->bodies)
    if (record.settings.type == BodyType::Static && !record.settings.sensor)
      bounds.Encapsulate(bodyInterface.GetTransformedShape(record.id).GetWorldSpaceBounds());
  if (!bounds.IsValid())
    return false;
  min = toGlm(bounds.mMin);
  max = toGlm(bounds.mMax);
  return true;
}

const std::vector<PhysicsContact> &PhysicsSystem::getContacts() const { return m_impl->contacts; }

size_t PhysicsSystem::getBodyCount() const { return m_impl->bodies.size(); }
