#pragma once
// Skeletal animation: poses the skeleton of every animated model with an Animator from its clip (crossfading between
// two while switching) and keeps the joint matrices the renderer skins with.

#include "foundation/ecs/componentManager.h"
#include "rendering/resources/skeletonPose.h"
#include "foundation/ecs/systemManager.h"
#include <filesystem>
#include <cstdint>
#include <glm/glm.hpp>
#include "systems/animationGraph.h"
#include <map>
#include <memory>
#include <tuple>
#include <string>
#include <unordered_map>
#include <vector>

class ResourceSystem;
struct MeshAnimation;
struct AnimationClip;
struct NodePose;
struct SkeletonNode;
struct Animator;
struct AnimationStateMachine;

class AnimationSystem : public BaseSystem {
public:
  AnimationSystem(ComponentManager &componentManager, ResourceSystem &resourceSystem)
      : m_componentManager(componentManager), m_resourceSystem(resourceSystem) {}

  // Runs every AnimationStateMachine (parameters read, transitions fired), then advances every Animator by deltaTime
  // (0 while paused: it holds its pose, and the machines wait)
  void update(float deltaTime);

  // A state machine's definition, read from the file once and again after it changes on disk; null for a file that is
  // missing or invalid (logged once, with the reason). The pointer stays good while the caller holds it.
  std::shared_ptr<const AnimationGraph> graph(const std::string &path);
  // Puts the entity's state machine in its default state if it has not started (the Animator is added if needed)
  void startMachine(Entity entity);
  // Makes the machine start over from the default state with every parameter at the file's initial value
  void restartMachine(Entity entity);

  // The state changes since the last call, oldest first: the entity, the layer ("base" for the top-level states), the
  // state it left and the one it entered
  struct StateChange {
    Entity entity;
    std::string layer, from, to;
  };
  std::vector<StateChange> takeStateChanges();

  // Switches the entity's animator to `clip`, fading from what it plays over `fadeSeconds` (none when 0); the time
  // starts over. What it fades out is what is on screen: a blend is faded out whole, the clip keeps its own speed and
  // loop, and switching again in the middle of a fade fades out the pose the fade had reached, so nothing jumps.
  // `animation` is the model's, for names and lengths (may be null). `layer` is 0 for the Animator's own playback, a
  // state machine layer's number plus one for the playback of that layer.
  void crossfade(Entity entity, Animator &animator, const MeshAnimation *animation, const std::string &clip,
                 float fadeSeconds, bool loop, int layer = 0);

  // The joint matrices to skin the entity's model with this frame: its Animator's pose, or the model's rest pose
  // when it has none; null for a static model
  const std::vector<glm::mat4> *getJointMatrices(Entity entity) const;

  // The pose of `clip` at `time` (seconds, wrapped when looping) over the rest pose
  static void sample(const MeshAnimation &animation, const AnimationClip &clip, float time, std::vector<NodePose> &pose);

private:
  ComponentManager &m_componentManager;
  ResourceSystem &m_resourceSystem;
  struct CachedGraph {
    std::shared_ptr<const AnimationGraph> graph; // null when the file was not usable
    std::filesystem::file_time_type modified;
    bool exists = false;
  };
  std::unordered_map<std::string, CachedGraph> m_graphs;
  int m_graphCheck = 0; // frames since the definition files were last looked at
  std::vector<StateChange> m_changes;

  const MeshAnimation *animationOf(Entity entity) const;
  void enterState(Entity entity, AnimationStateMachine &machine, const AnimationGraph &graph, int state,
                  float fadeSeconds);
  void enterLayerState(Entity entity, AnimationStateMachine &machine, const AnimationGraph::Layer &layer, int layerIndex,
                       int state, float fadeSeconds);
  void runMachine(Entity entity, AnimationStateMachine &machine, float deltaTime);
  void reloadChangedGraphs();

  // Posing: one playback (the Animator's, or a layer's) advanced by deltaTime and its pose written to `pose` (not when
  // `wanted` is false: only the clocks move). False when its clip is not in the model.
  bool evaluate(Entity entity, int slot, Animator &playback, const MeshAnimation &animation, float deltaTime,
                bool wanted, std::vector<NodePose> &pose);
  void applyLayers(Entity entity, AnimationStateMachine &machine, const std::shared_ptr<const AnimationGraph> &graph,
                   uint32_t meshHandle, const MeshAnimation &animation, float deltaTime, std::vector<NodePose> &pose);
  // The weight of each node of the skeleton for a layer's mask (empty: every node at 1), worked out once per graph
  // and model
  const std::vector<float> &maskWeights(const std::shared_ptr<const AnimationGraph> &graph, size_t layer,
                                        uint32_t meshHandle, const MeshAnimation &animation);

  // The poses a playback keeps while it fades: the last one it showed (what an interrupting fade starts from) and the
  // frozen one it fades out when interrupted. Only while a fade runs; gone with the entity, like the joints.
  struct Snapshot {
    std::vector<NodePose> last, frozen;
    bool seen = false;
  };
  std::unordered_map<uint64_t, Snapshot> m_snapshots;
  static uint64_t snapshotKey(Entity entity, int slot) { return (static_cast<uint64_t>(entity) << 16) | slot; }

  struct MaskKey {
    const AnimationGraph *graph;
    size_t layer;
    uint32_t mesh;
    bool operator<(const MaskKey &o) const {
      return std::tie(graph, layer, mesh) < std::tie(o.graph, o.layer, o.mesh);
    }
  };
  // Holding the graph keeps its address from being reused by another one while an entry points at it
  struct CachedMask {
    std::shared_ptr<const AnimationGraph> graph;
    std::vector<float> weights;
  };
  std::map<MaskKey, CachedMask> m_masks;

  std::unordered_map<Entity, std::vector<glm::mat4>> m_joints;
  std::vector<glm::mat4> m_globals; // scratch
  std::vector<NodePose> m_scratch, m_fadePose, m_layerPose, m_referencePose; // scratch
};
