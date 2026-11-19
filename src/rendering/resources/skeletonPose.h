#pragma once
// A skeleton's pose: each node's position, rotation and scale relative to its parent, and the joint matrices the
// vertex shader skins with. Shared by the glTF loader (the rest pose) and the AnimationSystem (sampled clips).

#include "rendering/resources/mesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

struct NodePose {
  glm::vec3 translation{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};
};

namespace SkeletonPose {

inline std::vector<NodePose> rest(const Skeleton &skeleton) {
  std::vector<NodePose> pose(skeleton.nodes.size());
  for (size_t i = 0; i < pose.size(); ++i)
    pose[i] = {skeleton.nodes[i].translation, skeleton.nodes[i].rotation, skeleton.nodes[i].scale};
  return pose;
}

// Model-space matrix of every joint times its inverse bind matrix: what moves a vertex from its rest place
inline void jointMatrices(const Skeleton &skeleton, const std::vector<NodePose> &pose, std::vector<glm::mat4> &globals,
                          std::vector<glm::mat4> &joints) {
  globals.resize(skeleton.nodes.size());
  for (int node : skeleton.order) {
    const NodePose &local = pose[node];
    glm::mat4 matrix = glm::translate(glm::mat4(1.0f), local.translation) * glm::mat4_cast(local.rotation);
    matrix = glm::scale(matrix, local.scale);
    const int parent = skeleton.nodes[node].parent;
    globals[node] = parent >= 0 ? globals[parent] * matrix : matrix;
  }
  joints.resize(skeleton.jointNodes.size());
  for (size_t j = 0; j < joints.size(); ++j)
    joints[j] = globals[skeleton.jointNodes[j]] * skeleton.inverseBind[j];
}

} // namespace SkeletonPose
