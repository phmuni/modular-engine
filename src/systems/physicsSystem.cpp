// Jolt-backed physics: bodies and characters kept in step with the ECS.

#include "systems/physicsSystem.h"

#include "components/characterController.h"
#include "components/joint.h"
#include "components/model.h"
#include "components/name.h"
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
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <functional>
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

// Collects the contacts that began and ended; Jolt calls it from its worker threads. Jolt reports a contact per pair of
// sub-shapes (a box on a mesh touches many triangles), so the touching sub-shapes of each pair of bodies are counted:
// the pair begins with the first and ends with the last. OnContactRemoved gives only body IDs, and the bodies may be
// gone by then (it cannot lock them), so what the pair needs to be reported is kept from when it began.
//
// Jolt also drops the contacts of bodies that fall asleep, though they still rest on each other: those pairs are kept
// as dormant instead of ending, and begin again silently when Jolt reports the contact again (something woke them).
// A dormant pair with a body that is awake and no contact back is a pair that really separated (a body lifted away
// while asleep), and ends.
class ContactCollector final : public JPH::ContactListener {
public:
  struct Pair {
    int count = 0; // touching sub-shapes
    Entity a = 0, b = 0;
    JPH::BodyID idA, idB;
    int awakeUpdates = 0; // dormant pairs: updates with a body awake and no contact back
  };

  // Two bodies joined by a Joint with collideConnected off do not collide with each other: Jolt asks here before it
  // makes a contact (a group filter would do the same, but needs every body built with one and a filter class of its
  // own, for a rule that touches a few bodies). The pairs change on the main thread between steps, when Jolt's workers
  // are idle, so reading them needs no lock; most scenes have none, which is a single empty() check per candidate pair.
  JPH::ValidateResult OnContactValidate(const JPH::Body &a, const JPH::Body &b, JPH::RVec3Arg,
                                        const JPH::CollideShapeResult &) override {
    if (!ignored.empty() && ignored.count(entityKey(static_cast<Entity>(a.GetUserData()),
                                                    static_cast<Entity>(b.GetUserData()))) > 0)
      return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
  }
  void ignorePair(Entity a, Entity b) { ++ignored[entityKey(a, b)]; }
  void allowPair(Entity a, Entity b) {
    const auto it = ignored.find(entityKey(a, b));
    if (it != ignored.end() && --it->second <= 0)
      ignored.erase(it);
  }

  void OnContactAdded(const JPH::Body &a, const JPH::Body &b, const JPH::ContactManifold &manifold,
                      JPH::ContactSettings &) override {
    const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
    const JPH::Vec3 normal = manifold.mWorldSpaceNormal;
    const uint64_t key = keyOf(a.GetID(), b.GetID());
    std::lock_guard<std::mutex> lock(mutex);
    Pair &pair = pairs[key];
    if (pair.count++ > 0)
      return; // these bodies already touch somewhere else
    pair.a = static_cast<Entity>(a.GetUserData());
    pair.b = static_cast<Entity>(b.GetUserData());
    pair.idA = a.GetID();
    pair.idB = b.GetID();
    if (dormant.erase(key) > 0)
      return; // they were only asleep, not new
    contacts.push_back({pair.a, pair.b, glm::vec3(point.GetX(), point.GetY(), point.GetZ()),
                        glm::vec3(normal.GetX(), normal.GetY(), normal.GetZ())});
  }

  void OnContactRemoved(const JPH::SubShapeIDPair &subShapes) override {
    const uint64_t key = keyOf(subShapes.GetBody1ID(), subShapes.GetBody2ID());
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = pairs.find(key);
    if (it == pairs.end() || --it->second.count > 0)
      return;
    ended.push_back({key, it->second});
    pairs.erase(it);
  }

  // After an update, on the main thread: hands over the contacts that began and the pairs that stopped touching.
  // `owns` says whether an entity still has that very body; a pair of bodies that no longer exist is not reported (the
  // entity number may already belong to another entity). `stepped` is whether the simulation advanced.
  void collect(const JPH::BodyInterface &bodyInterface, bool stepped,
               const std::function<bool(Entity, JPH::BodyID)> &owns, std::vector<PhysicsContact> &began,
               std::vector<PhysicsContactEnd> &ends) {
    std::lock_guard<std::mutex> lock(mutex);
    began.swap(contacts);
    contacts.clear();
    auto asleep = [&](const Pair &pair) { return !bodyInterface.IsActive(pair.idA) && !bodyInterface.IsActive(pair.idB); };
    for (const Ended &end : ended) {
      if (!owns(end.pair.a, end.pair.idA) || !owns(end.pair.b, end.pair.idB))
        continue;
      if (asleep(end.pair))
        dormant[end.key] = end.pair;
      else
        ends.push_back({end.pair.a, end.pair.b});
    }
    ended.clear();
    if (!stepped)
      return;
    for (auto it = dormant.begin(); it != dormant.end();) {
      Pair &pair = it->second;
      if (!owns(pair.a, pair.idA) || !owns(pair.b, pair.idB)) {
        it = dormant.erase(it);
      } else if (!asleep(pair) && ++pair.awakeUpdates >= 2) { // a step may wake it before its contact is found again
        ends.push_back({pair.a, pair.b});
        it = dormant.erase(it);
      } else {
        if (asleep(pair))
          pair.awakeUpdates = 0;
        ++it;
      }
    }
  }

  // Forgets everything (the bodies were all removed)
  void clear() {
    std::lock_guard<std::mutex> lock(mutex);
    pairs.clear();
    dormant.clear();
    contacts.clear();
    ended.clear();
  }

private:
  struct Ended {
    uint64_t key;
    Pair pair;
  };
  // The pair of bodies, the same whichever order Jolt names them in
  static uint64_t keyOf(JPH::BodyID a, JPH::BodyID b) {
    const uint32_t first = std::min(a.GetIndexAndSequenceNumber(), b.GetIndexAndSequenceNumber());
    const uint32_t second = std::max(a.GetIndexAndSequenceNumber(), b.GetIndexAndSequenceNumber());
    return (static_cast<uint64_t>(first) << 32) | second;
  }

  static uint64_t entityKey(Entity a, Entity b) {
    const uint32_t first = static_cast<uint32_t>(std::min(a, b));
    const uint32_t second = static_cast<uint32_t>(std::max(a, b));
    return (static_cast<uint64_t>(first) << 32) | second;
  }

  std::unordered_map<uint64_t, int> ignored; // pairs of entities that do not collide, counted per joint
  std::mutex mutex;
  std::vector<PhysicsContact> contacts;
  std::vector<Ended> ended;
  std::unordered_map<uint64_t, Pair> pairs;   // touching, in Jolt's contact list
  std::unordered_map<uint64_t, Pair> dormant; // touching, asleep: not in Jolt's list
};

// Skips what a query's options leave out; the character controllers' inner bodies unless `characters` is set
class QueryFilter final : public JPH::BodyFilter {
public:
  QueryFilter(const QueryOptions &options, const std::function<bool(const JPH::Body &)> &isCharacter)
      : m_options(options), m_isCharacter(isCharacter) {}
  bool ShouldCollideLocked(const JPH::Body &body) const override {
    if (!m_options.characters && m_isCharacter(body))
      return false;
    return m_options.accepts(static_cast<Entity>(body.GetUserData()));
  }

private:
  const QueryOptions &m_options;
  const std::function<bool(const JPH::Body &)> &m_isCharacter;
};

JPH::Vec3 toJolt(const glm::vec3 &v) { return JPH::Vec3(v.x, v.y, v.z); }
glm::vec3 toGlm(const JPH::Vec3 &v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
JPH::Quat toJolt(const glm::quat &q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
glm::quat toGlm(const JPH::Quat &q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

// The same order as TransformSystem::calculateModelMatrix: Y, then X, then Z
glm::quat rotationOf(const glm::vec3 &degrees) {
  const glm::vec3 radians = glm::radians(degrees);
  return glm::angleAxis(radians.y, glm::vec3(0, 1, 0)) * glm::angleAxis(radians.x, glm::vec3(1, 0, 0)) *
         glm::angleAxis(radians.z, glm::vec3(0, 0, 1));
}
glm::quat rotationOf(const Transform &transform) { return rotationOf(transform.rotation); }

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
    // A static body that game code keeps moving (see warnIfStaticMoves)
    uint64_t lastMovedStep = 0;
    int movedSteps = 0;
    bool warnedMoving = false;
  };
  std::unordered_map<Entity, BodyRecord> bodies;

  // Velocity, impulse and force set on an entity whose body Jolt does not have yet (created this frame): applied when
  // sync builds it. Empty almost always, so it costs nothing per frame.
  struct PendingMotion {
    std::optional<glm::vec3> linear, angular;
    glm::vec3 impulse{0.0f}, force{0.0f}, torque{0.0f}, angularImpulse{0.0f};
    // Pushes off the center: (vector, world point), applied in order
    std::vector<std::pair<glm::vec3, glm::vec3>> impulsesAt, forcesAt;
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

  // A joint's constraint, and what it was built from. The record exists for every Joint component, built or not: a
  // joint waits for its target by name (resolved like a CameraRig's) and for both bodies.
  struct JointRecord {
    Joint built;                                 // the component as the constraint was built or last updated from it
    JPH::Ref<JPH::TwoBodyConstraint> constraint; // null until built
    Entity connected = -1;                       // the entity whose body is the other end (-1: the world), once built
    bool pairIgnored = false;                    // collideConnected is off: the pair is in the contact collector
    Entity resolved = -1;                        // what `target` named when last looked up
    std::string resolvedName;
    uint64_t nextLookup = 0; // jointTicks at which a name that was not found is looked up again
    bool warned = false;     // the reason it is not built was logged
  };
  std::unordered_map<Entity, JointRecord> joints;
  uint64_t jointTicks = 0; // syncs so far, for the lookup retry

  struct CharacterRecord {
    JPH::Ref<JPH::CharacterVirtual> character;
    CharacterController settings; // the shape part, to rebuild it
    Transform written;
    std::string layerName; // what layerIndex was resolved from, and when
    uint32_t layerVersion = 0;
    int layerIndex = 0;
  };
  std::unordered_map<Entity, CharacterRecord> characters;

  std::vector<PhysicsContact> contacts;       // from the last update
  std::vector<PhysicsContactEnd> contactEnds; // from the last update
  float accumulator = 0.0f;
  uint64_t step = 0;       // simulation steps so far
  bool simulating = false; // sync is running inside a step, not for a paused or edited scene

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

  // A character's inner body (see createCharacter)
  bool isCharacter(const JPH::Body &body) const {
    const auto it = characters.find(static_cast<Entity>(body.GetUserData()));
    return it != characters.end() && it->second.character->GetInnerBodyID() == body.GetID();
  }

  // The entities whose bodies overlap a shape at a place, each once (an entity with several parts is one)
  std::vector<Entity> overlapping(const JPH::Shape &shape, const glm::vec3 &center, const glm::quat &rotation,
                                  const QueryOptions &options) const {
    const std::function<bool(const JPH::Body &)> isCharacterBody = [this](const JPH::Body &body) {
      return isCharacter(body);
    };
    const QueryFilter filter(options, isCharacterBody);
    const LayerMaskFilter layerFilter(options.layerMask);
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    system->GetNarrowPhaseQuery().CollideShape(
        &shape, JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sRotationTranslation(toJolt(rotation), JPH::RVec3(toJolt(center))), JPH::CollideShapeSettings(),
        JPH::RVec3::sZero(), collector, {}, layerFilter, filter);
    std::vector<Entity> found;
    for (const JPH::CollideShapeResult &hit : collector.mHits) {
      JPH::BodyLockRead lock(system->GetBodyLockInterface(), hit.mBodyID2);
      if (!lock.Succeeded())
        continue;
      const Entity entity = static_cast<Entity>(lock.GetBody().GetUserData());
      if (std::find(found.begin(), found.end(), entity) == found.end())
        found.push_back(entity);
    }
    return found;
  }

  // Whether the entity still has this very body (not destroyed, nor rebuilt as another since)
  bool hasBody(Entity entity, JPH::BodyID id) const {
    if (const auto it = bodies.find(entity); it != bodies.end())
      return it->second.id == id;
    if (const auto it = characters.find(entity); it != characters.end())
      return it->second.character->GetInnerBodyID() == id;
    return false;
  }

  void removeAll() {
    JPH::BodyInterface &bodyInterface = system->GetBodyInterface();
    for (auto &[entity, record] : joints) // before the bodies they hold
      releaseJoint(entity, record);
    joints.clear();
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
      // A mirroring scale (an odd number of negative axes) turns the triangles inside out: the winding is kept
      // counter-clockwise seen from outside, which collision does not need but the navigation mesh does
      const bool mirrored = scale.x * scale.y * scale.z < 0.0f;
      for (size_t i = 0; i + 2 < indices.size(); i += 3)
        triangles.push_back(mirrored ? JPH::IndexedTriangle(indices[i], indices[i + 2], indices[i + 1])
                                     : JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));
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
        bodyInterface.AddTorque(id, toJolt(motion.torque));
        bodyInterface.AddAngularImpulse(id, toJolt(motion.angularImpulse));
        for (const auto &[impulse, point] : motion.impulsesAt)
          bodyInterface.AddImpulse(id, toJolt(impulse), JPH::RVec3(toJolt(point)));
        for (const auto &[force, point] : motion.forcesAt)
          bodyInterface.AddForce(id, toJolt(force), JPH::RVec3(toJolt(point)));
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
    // A kinematic capsule that it carries along: without it, the character is not in the physics world, so dynamic
    // bodies fall and roll through it (it then shoves them out slowly). A little wider than the character's own
    // capsule, so what rests on it stays out of the character's padding: a box sunk into its head with the floor under
    // its feet would pin it in place. The character itself skips it; raycasts and the navigation's capsule test skip
    // it too (see isCharacter).
    settings.mInnerBodyShape = new JPH::CapsuleShape(halfCylinder, radius + 0.05f);
    settings.mInnerBodyLayer = Layers::objectLayer(layers.resolve(controller.layer), true);
    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
        &settings, JPH::RVec3(toJolt(transform.position)), JPH::Quat::sIdentity(), static_cast<JPH::uint64>(entity),
        system.get());
    characters[entity] = CharacterRecord{character, controller, transform, controller.layer, layers.version(),
                                         layers.resolve(controller.layer)};
  }

  // Bodies and characters for the components there are now, with Transforms moved from outside handed over
  // A static body jumps to where game code moves it, with no velocity: what it hits is pushed out of it but gets no
  // push, which looks like broken physics. Moving it once (a level change) is fine; moving it frame after frame (a
  // spinning wall, a moving platform) means it should be kinematic: said once per body. Only while simulating, so
  // dragging it in the editor does not count.
  void warnIfStaticMoves(Entity entity, BodyRecord &record) {
    if (!simulating || record.warnedMoving)
      return;
    constexpr uint64_t kGapSteps = 4; // a frame can hold several steps (two at 30 fps) and only its first sees the move
    constexpr int kWarnAfterMoves = 15; // a quarter of a second at 60 fps
    const bool continued = record.movedSteps > 0 && step - record.lastMovedStep <= kGapSteps;
    record.movedSteps = continued ? record.movedSteps + 1 : 1;
    record.lastMovedStep = step;
    if (record.movedSteps < kWarnAfterMoves)
      return;
    record.warnedMoving = true;
    const Name *name = componentManager.tryGet<Name>(entity);
    const std::string who = name && !name->name.empty() ? "\"" + name->name + "\" (entity " + std::to_string(entity) + ")"
                                                        : "Entity " + std::to_string(entity);
    LOG_W("[Physics] %s is a static body that keeps being moved: it pushes what it meets without giving it any "
          "velocity. Make its RigidBody kinematic for moving walls, platforms and doors",
          who.c_str());
  }

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
        releaseJointsOf(it->first);
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
        releaseJointsOf(entity); // built again after the new body, below
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
        // Moved by game code or the editor: the body jumps there, and its joints are built again where it is now
        releaseJointsOf(entity);
        bodyInterface.SetPositionAndRotation(record.id, position, rotation,
                                             body.type == BodyType::Static ? JPH::EActivation::DontActivate
                                                                           : JPH::EActivation::Activate);
        record.written = *transform;
        if (body.type == BodyType::Static)
          warnIfStaticMoves(entity, record);
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

    syncJoints();
  }

  // ---- Joints ----

  // Jolt lets a body sleep when it stops, which would leave a motor that is pushing on a stalled body idle until
  // something woke it: bodies a motor drives never sleep
  void setSleepAllowed(Entity entity, bool allowed) {
    const auto it = bodies.find(entity);
    if (it == bodies.end())
      return;
    JPH::BodyLockWrite lock(system->GetBodyLockInterface(), it->second.id);
    if (lock.Succeeded())
      lock.GetBody().SetAllowSleeping(allowed);
  }

  void wakeBody(Entity entity) {
    const auto it = bodies.find(entity);
    if (it != bodies.end() && it->second.settings.type != BodyType::Static)
      system->GetBodyInterface().ActivateBody(it->second.id);
  }

  // Takes the constraint out of the world (the record stays, to be built again)
  void releaseJoint(Entity owner, JointRecord &record) {
    if (record.constraint) {
      system->RemoveConstraint(record.constraint);
      record.constraint = nullptr;
      setSleepAllowed(owner, true);
      wakeBody(owner);
      if (record.connected != -1)
        wakeBody(record.connected);
    }
    if (record.pairIgnored && record.connected != -1)
      contactCollector.allowPair(owner, record.connected);
    record.pairIgnored = false;
    record.connected = -1;
  }

  // Before a body goes away (or is built again, or jumps): the constraints that hold it, as owner or as target
  void releaseJointsOf(Entity entity) {
    if (joints.empty())
      return;
    for (auto &[owner, record] : joints)
      if (record.constraint && (owner == entity || record.connected == entity))
        releaseJoint(owner, record);
  }

  void warnJointOnce(Entity owner, JointRecord &record, const char *reason) {
    if (record.warned)
      return;
    record.warned = true;
    const Name *name = componentManager.tryGet<Name>(owner);
    LOG_W("[Physics] %s%s%s: joint not built yet: %s", name && !name->name.empty() ? "\"" : "Entity ",
          name && !name->name.empty() ? name->name.c_str() : std::to_string(owner).c_str(),
          name && !name->name.empty() ? "\"" : "", reason);
  }

  // The entity a joint's target name names (the lowest entity number with a Transform, like a CameraRig's target);
  // looked up again when the name changes or the entity is gone, but not every step while it is missing
  bool findJointTarget(JointRecord &record, const std::string &name) {
    if (record.resolved != -1 && record.resolvedName == name) {
      const Name *current = componentManager.tryGet<Name>(record.resolved);
      if (current && current->name == name && componentManager.has<Transform>(record.resolved))
        return true;
    }
    if (record.resolvedName == name && jointTicks < record.nextLookup)
      return false;
    record.nextLookup = jointTicks + 15;
    record.resolvedName = name;
    Entity found = -1;
    componentManager.forEachComponent<Name>([&](Entity entity, Name &candidate) {
      if (candidate.name == name && (found == -1 || entity < found) && componentManager.has<Transform>(entity))
        found = entity;
    });
    record.resolved = found;
    return found != -1;
  }

  // A body's pose as Jolt has it, with the entity's scale: where an anchor in its own space is in the world
  struct Frame {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
    glm::vec3 point(const glm::vec3 &local) const { return position + rotation * (local * scale); }
  };
  Frame frameOf(const BodyRecord &record) {
    JPH::RVec3 position;
    JPH::Quat rotation;
    system->GetBodyInterface().GetPositionAndRotation(record.id, position, rotation);
    return {toGlm(JPH::Vec3(position)), toGlm(rotation), record.scale};
  }

  // Pushes the motor, spring and force limit onto the live constraint, and wakes the bodies when it changes (a sleeping
  // body would not feel a motor that was just switched on)
  void applyJointMotor(Entity owner, JointRecord &record, const Joint &joint, bool wake) {
    const float frequency = joint.springFrequency > 0.0f ? joint.springFrequency : 10.0f; // a position motor needs one
    const JPH::SpringSettings spring(JPH::ESpringMode::FrequencyAndDamping, frequency, std::max(joint.springDamping, 0.0f));
    const float force = std::max(joint.maxMotorForce, 0.0f);
    JPH::EMotorState state = JPH::EMotorState::Off;
    if (joint.motor == JointMotor::Velocity)
      state = JPH::EMotorState::Velocity;
    else if (joint.motor == JointMotor::Position)
      state = JPH::EMotorState::Position;
    bool driven = false;
    switch (joint.type) {
    case JointType::Hinge: {
      auto &hinge = static_cast<JPH::HingeConstraint &>(*record.constraint);
      hinge.GetMotorSettings().mSpringSettings = spring;
      hinge.GetMotorSettings().SetTorqueLimit(force);
      hinge.SetTargetAngularVelocity(glm::radians(joint.motorSpeed));
      hinge.SetTargetAngle(glm::radians(joint.motorTarget));
      hinge.SetMotorState(state);
      driven = state != JPH::EMotorState::Off;
      break;
    }
    case JointType::Slider: {
      auto &slider = static_cast<JPH::SliderConstraint &>(*record.constraint);
      slider.GetMotorSettings().mSpringSettings = spring;
      slider.GetMotorSettings().SetForceLimit(force);
      slider.SetTargetVelocity(joint.motorSpeed);
      slider.SetTargetPosition(joint.motorTarget);
      slider.SetMotorState(state);
      driven = state != JPH::EMotorState::Off;
      break;
    }
    case JointType::Distance: {
      // The spring softens the limits; 0 keeps them hard
      auto &distance = static_cast<JPH::DistanceConstraint &>(*record.constraint);
      distance.SetLimitsSpringSettings(JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping,
                                                           std::max(joint.springFrequency, 0.0f),
                                                           std::max(joint.springDamping, 0.0f)));
      break;
    }
    case JointType::Fixed:
      break;
    }
    setSleepAllowed(owner, !driven);
    if (wake) {
      wakeBody(owner);
      if (record.connected != -1)
        wakeBody(record.connected);
    }
  }

  // The constraint for a Joint component, if both its bodies exist (a reason is logged once when they do not)
  void buildJoint(Entity owner, JointRecord &record, const Joint &joint) {
    const auto ownerBody = bodies.find(owner);
    if (ownerBody == bodies.end()) {
      warnJointOnce(owner, record, "the entity has no RigidBody to hold it");
      return;
    }
    Entity other = -1;
    const BodyRecord *otherBody = nullptr;
    if (!joint.target.empty()) {
      if (!findJointTarget(record, joint.target)) {
        warnJointOnce(owner, record, ("no entity is named \"" + joint.target + "\"").c_str());
        return;
      }
      other = record.resolved;
      const auto found = bodies.find(other);
      if (other == owner || found == bodies.end()) {
        warnJointOnce(owner, record,
                      other == owner ? "its target is the entity itself"
                                     : ("its target \"" + joint.target + "\" has no RigidBody").c_str());
        return;
      }
      otherBody = &found->second;
    }

    // Body 1 is the target (or the world) and body 2 this entity: Jolt measures a hinge's angle and a slider's position
    // of body 2 relative to body 1, so this way they grow in the direction the axis points (a hinge turns right-handed)
    const Frame mine = frameOf(ownerBody->second);
    const glm::vec3 pivot = mine.point(joint.anchor);
    glm::vec3 axis = mine.rotation * joint.axis;
    axis = glm::length(axis) > 1e-6f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
    const JPH::Vec3 axisJolt = toJolt(axis);
    const JPH::Vec3 normalJolt = axisJolt.GetNormalizedPerpendicular();
    const JPH::RVec3 pivotJolt(toJolt(pivot));

    JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
    switch (joint.type) {
    case JointType::Hinge: {
      auto *hinge = new JPH::HingeConstraintSettings();
      hinge->mSpace = JPH::EConstraintSpace::WorldSpace;
      hinge->mPoint1 = hinge->mPoint2 = pivotJolt;
      hinge->mHingeAxis1 = hinge->mHingeAxis2 = axisJolt;
      hinge->mNormalAxis1 = hinge->mNormalAxis2 = normalJolt;
      if (joint.limits) {
        hinge->mLimitsMin = std::clamp(glm::radians(joint.min), -JPH::JPH_PI, 0.0f);
        hinge->mLimitsMax = std::clamp(glm::radians(joint.max), 0.0f, JPH::JPH_PI);
        if (hinge->mLimitsMax - hinge->mLimitsMin < 1e-4f)
          hinge->mLimitsMax = hinge->mLimitsMin + 1e-4f; // no turning at all is a Fixed joint, but a range of 0 is a mistake
      }
      settings = hinge;
      break;
    }
    case JointType::Slider: {
      auto *slider = new JPH::SliderConstraintSettings();
      slider->mSpace = JPH::EConstraintSpace::WorldSpace;
      slider->mAutoDetectPoint = false;
      slider->mPoint1 = slider->mPoint2 = pivotJolt;
      slider->mSliderAxis1 = slider->mSliderAxis2 = axisJolt;
      slider->mNormalAxis1 = slider->mNormalAxis2 = normalJolt;
      if (joint.limits) {
        slider->mLimitsMin = std::min(joint.min, 0.0f);
        slider->mLimitsMax = std::max(joint.max, 0.0f);
        if (slider->mLimitsMax - slider->mLimitsMin < 1e-4f)
          slider->mLimitsMax = slider->mLimitsMin + 1e-4f;
      }
      settings = slider;
      break;
    }
    case JointType::Fixed: {
      auto *fixed = new JPH::FixedConstraintSettings();
      fixed->mSpace = JPH::EConstraintSpace::WorldSpace;
      fixed->mAutoDetectPoint = true; // the pose they are in now
      settings = fixed;
      break;
    }
    case JointType::Distance: {
      auto *distance = new JPH::DistanceConstraintSettings();
      distance->mSpace = JPH::EConstraintSpace::WorldSpace;
      distance->mPoint1 = JPH::RVec3(toJolt(otherBody ? frameOf(*otherBody).point(joint.targetAnchor)
                                                       : joint.targetAnchor));
      distance->mPoint2 = pivotJolt;
      if (joint.minDistance > 0.0f || joint.maxDistance > 0.0f) {
        distance->mMinDistance = std::max(joint.minDistance, 0.0f);
        distance->mMaxDistance = std::max(joint.maxDistance, distance->mMinDistance);
      } // else both stay -1: the distance the points are at now
      if (joint.springFrequency > 0.0f)
        distance->mLimitsSpringSettings = JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping,
                                                              joint.springFrequency, std::max(joint.springDamping, 0.0f));
      settings = distance;
      break;
    }
    }

    JPH::Ref<JPH::TwoBodyConstraint> constraint;
    if (otherBody) {
      const JPH::BodyID ids[2] = {otherBody->id, ownerBody->second.id};
      JPH::BodyLockMultiWrite lock(system->GetBodyLockInterface(), ids, 2);
      if (!lock.GetBody(0) || !lock.GetBody(1))
        return;
      constraint = settings->Create(*lock.GetBody(0), *lock.GetBody(1));
    } else {
      JPH::BodyLockWrite lock(system->GetBodyLockInterface(), ownerBody->second.id);
      if (!lock.Succeeded())
        return;
      constraint = settings->Create(JPH::Body::sFixedToWorld, lock.GetBody());
    }
    if (!constraint)
      return;
    system->AddConstraint(constraint);
    record.constraint = constraint;
    record.built = joint;
    record.connected = other;
    record.warned = false;
    if (other != -1 && !joint.collideConnected) {
      contactCollector.ignorePair(owner, other);
      record.pairIgnored = true;
    }
    applyJointMotor(owner, record, joint, true);
  }

  void syncJoints() {
    ++jointTicks;
    for (auto it = joints.begin(); it != joints.end();) {
      if (!componentManager.has<Joint>(it->first) || !componentManager.has<Transform>(it->first)) {
        releaseJoint(it->first, it->second);
        it = joints.erase(it);
      } else {
        ++it;
      }
    }
    componentManager.forEachComponent<Joint>([&](Entity owner, Joint &joint) {
      if (!componentManager.has<Transform>(owner))
        return;
      JointRecord &record = joints[owner];
      if (record.constraint) {
        // Built again for what it was built from, or when its target was renamed (the name is what it follows)
        const Name *targetName = record.connected != -1 ? componentManager.tryGet<Name>(record.connected) : nullptr;
        if (record.built != joint || (record.connected != -1 && (!targetName || targetName->name != joint.target))) {
          releaseJoint(owner, record);
          record.warned = false;
        } else if (!record.built.sameMotor(joint)) {
          record.built = joint;
          applyJointMotor(owner, record, joint, true);
        }
      }
      if (!record.constraint)
        buildJoint(owner, record, joint);
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
        system->GetBodyInterface().SetObjectLayer(character.GetInnerBodyID(),
                                                  Layers::objectLayer(record.layerIndex, true));
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
  impl.contactEnds.clear();
  if (deltaTime <= 0.0f) {
    impl.sync(kStep); // paused: bodies still follow edits, nothing simulates
    return;
  }
  impl.accumulator = std::min(impl.accumulator + deltaTime, kStep * kMaxStepsPerUpdate);
  bool stepped = false;
  while (impl.accumulator >= kStep) {
    stepped = true;
    impl.accumulator -= kStep;
    ++impl.step;
    impl.simulating = true;
    impl.sync(kStep);
    impl.simulating = false;
    impl.updateCharacters(kStep);
    impl.system->Update(kStep, 1, impl.tempAllocator.get(), impl.jobSystem.get());
    impl.writeBack();
  }
  impl.contactCollector.collect(
      impl.system->GetBodyInterface(), stepped, [&impl](Entity entity, JPH::BodyID id) { return impl.hasBody(entity, id); },
      impl.contacts, impl.contactEnds);
}

void PhysicsSystem::syncBodies() { m_impl->sync(kStep); }

void PhysicsSystem::reset() {
  m_impl->removeAll();
  m_impl->accumulator = 0.0f;
  m_impl->contacts.clear();
  m_impl->contactEnds.clear();
  m_impl->contactCollector.clear();
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
    pending->impulsesAt.clear();
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
  else if (auto *pending = m_impl->pendingFor(entity)) {
    pending->angular = velocity;
    pending->angularImpulse = glm::vec3(0.0f); // as for the linear velocity: it replaces an earlier angular impulse
  }
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

void PhysicsSystem::addTorque(Entity entity, const glm::vec3 &torque) {
  if (const auto *record = m_impl->dynamicBody(entity))
    m_impl->system->GetBodyInterface().AddTorque(record->id, toJolt(torque));
  else if (auto *pending = m_impl->pendingFor(entity))
    pending->torque += torque;
}

void PhysicsSystem::addAngularImpulse(Entity entity, const glm::vec3 &impulse) {
  if (const auto *record = m_impl->dynamicBody(entity))
    m_impl->system->GetBodyInterface().AddAngularImpulse(record->id, toJolt(impulse));
  else if (auto *pending = m_impl->pendingFor(entity))
    pending->angularImpulse += impulse;
}

void PhysicsSystem::addImpulseAt(Entity entity, const glm::vec3 &impulse, const glm::vec3 &worldPoint) {
  if (const auto *record = m_impl->dynamicBody(entity))
    m_impl->system->GetBodyInterface().AddImpulse(record->id, toJolt(impulse), JPH::RVec3(toJolt(worldPoint)));
  else if (auto *pending = m_impl->pendingFor(entity))
    pending->impulsesAt.emplace_back(impulse, worldPoint);
}

void PhysicsSystem::addForceAt(Entity entity, const glm::vec3 &force, const glm::vec3 &worldPoint) {
  if (const auto *record = m_impl->dynamicBody(entity))
    m_impl->system->GetBodyInterface().AddForce(record->id, toJolt(force), JPH::RVec3(toJolt(worldPoint)));
  else if (auto *pending = m_impl->pendingFor(entity))
    pending->forcesAt.emplace_back(force, worldPoint);
}

std::optional<RayHit> PhysicsSystem::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                             const std::function<bool(Entity)> &accept, uint32_t layerMask) const {
  QueryOptions options;
  options.accept = accept;
  options.layerMask = layerMask;
  return raycast(origin, direction, maxDistance, options);
}

std::optional<RayHit> PhysicsSystem::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                             const QueryOptions &options) const {
  if (glm::length(direction) <= 0.0f || maxDistance <= 0.0f)
    return std::nullopt;
  const glm::vec3 dir = glm::normalize(direction);
  const JPH::RRayCast ray(JPH::RVec3(toJolt(origin)), toJolt(dir * maxDistance));

  const std::function<bool(const JPH::Body &)> isCharacter = [this](const JPH::Body &body) {
    return m_impl->isCharacter(body);
  };
  const QueryFilter filter(options, isCharacter);

  JPH::RayCastResult hit;
  const LayerMaskFilter layerFilter(options.layerMask);
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

std::vector<Entity> PhysicsSystem::overlapSphere(const glm::vec3 &center, float radius,
                                                 const QueryOptions &options) const {
  const JPH::SphereShape shape(std::max(radius, 0.001f));
  return m_impl->overlapping(shape, center, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), options);
}

std::vector<Entity> PhysicsSystem::overlapBox(const glm::vec3 &center, const glm::vec3 &halfExtents,
                                              const glm::vec3 &rotation, const QueryOptions &options) const {
  const glm::vec3 half = glm::max(glm::abs(halfExtents), glm::vec3(0.001f));
  const JPH::BoxShape shape(toJolt(half), 0.0f); // no rounding: the box is exactly the size asked for
  return m_impl->overlapping(shape, center, rotationOf(rotation), options);
}

bool PhysicsSystem::isCapsuleBlocked(const glm::vec3 &base, float height, float radius) const {
  radius = std::max(radius, 0.01f);
  const float halfCylinder = std::max(height * 0.5f - radius, 0.01f);

  // Only what stays put and blocks (not a character standing there)
  class Filter final : public JPH::BodyFilter {
  public:
    explicit Filter(const Impl &impl) : m_impl(impl) {}
    bool ShouldCollideLocked(const JPH::Body &body) const override {
      return body.GetMotionType() != JPH::EMotionType::Dynamic && !body.IsSensor() && !m_impl.isCharacter(body);
    }

  private:
    const Impl &m_impl;
  } filter(*m_impl);

  const JPH::CapsuleShape shape(halfCylinder, radius);
  const glm::vec3 middle = base + glm::vec3(0.0f, halfCylinder + radius, 0.0f);
  JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
  m_impl->system->GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sReplicate(1.0f),
                                                     JPH::RMat44::sTranslation(JPH::RVec3(toJolt(middle))),
                                                     JPH::CollideShapeSettings(), JPH::RVec3::sZero(), collector, {}, {},
                                                     filter);
  return collector.HadHit();
}

void PhysicsSystem::collectTriangles(const glm::vec3 &min, const glm::vec3 &max, std::vector<glm::vec3> &vertices,
                                     std::vector<int> &indices) const {
  // The same bodies as isCapsuleBlocked: what stays put and blocks
  class Filter final : public JPH::BodyFilter {
  public:
    explicit Filter(const Impl &impl) : m_impl(impl) {}
    bool ShouldCollideLocked(const JPH::Body &body) const override {
      return body.GetMotionType() != JPH::EMotionType::Dynamic && !body.IsSensor() && !m_impl.isCharacter(body);
    }

  private:
    const Impl &m_impl;
  } filter(*m_impl);

  const JPH::AABox box(toJolt(glm::min(min, max)), toJolt(glm::max(min, max)));
  JPH::AllHitCollisionCollector<JPH::TransformedShapeCollector> collector;
  m_impl->system->GetNarrowPhaseQuery().CollectTransformedShapes(box, collector, {}, {}, filter);

  // Vertices come relative to the base offset: the box's middle keeps them precise far from the origin
  const JPH::RVec3 base = box.GetCenter();
  constexpr int kBatch = 64;
  JPH::Float3 batch[kBatch * 3];
  for (const JPH::TransformedShape &shape : collector.mHits) {
    JPH::Shape::GetTrianglesContext context;
    shape.GetTrianglesStart(context, box, base);
    while (const int count = shape.GetTrianglesNext(context, kBatch, batch)) {
      const int first = static_cast<int>(vertices.size());
      for (int i = 0; i < count * 3; ++i)
        vertices.push_back(toGlm(JPH::Vec3(batch[i])) + toGlm(JPH::Vec3(base)));
      for (int i = 0; i < count * 3; ++i)
        indices.push_back(first + i);
    }
  }
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
const std::vector<PhysicsContactEnd> &PhysicsSystem::getContactEnds() const { return m_impl->contactEnds; }

size_t PhysicsSystem::getBodyCount() const { return m_impl->bodies.size(); }

bool PhysicsSystem::isJointBuilt(Entity entity) const {
  const auto it = m_impl->joints.find(entity);
  return it != m_impl->joints.end() && it->second.constraint;
}

float PhysicsSystem::getJointAngle(Entity entity) const {
  const auto it = m_impl->joints.find(entity);
  if (it == m_impl->joints.end() || !it->second.constraint || it->second.built.type != JointType::Hinge)
    return 0.0f;
  return glm::degrees(static_cast<const JPH::HingeConstraint &>(*it->second.constraint).GetCurrentAngle());
}

float PhysicsSystem::getJointPosition(Entity entity) const {
  const auto it = m_impl->joints.find(entity);
  if (it == m_impl->joints.end() || !it->second.constraint || it->second.built.type != JointType::Slider)
    return 0.0f;
  return static_cast<const JPH::SliderConstraint &>(*it->second.constraint).GetCurrentPosition();
}
