#pragma once
// A skeleton's pose: each node's position, rotation and scale relative to its parent, and the joint matrices the
// vertex shader skins with. Shared by the glTF loader (the rest pose) and the AnimationSystem (sampled clips).

#include "rendering/resources/mesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace SkeletonPose {

inline std::vector<NodePose> rest(const Skeleton &skeleton) {
  std::vector<NodePose> pose(skeleton.nodes.size());
  for (size_t i = 0; i < pose.size(); ++i)
    pose[i] = {skeleton.nodes[i].translation, skeleton.nodes[i].rotation, skeleton.nodes[i].scale};
  return pose;
}

namespace detail {
// a * b where both are affine (last row 0, 0, 0, 1): the products with that row are left out
inline glm::mat4 affineProduct(const glm::mat4 &a, const glm::mat4 &b) {
  glm::mat4 r;
  for (int c = 0; c < 3; ++c)
    r[c] = a[0] * b[c].x + a[1] * b[c].y + a[2] * b[c].z;
  r[3] = a[0] * b[3].x + a[1] * b[3].y + a[2] * b[3].z + a[3];
  return r;
}

inline bool isAffine(const glm::mat4 &m) { return m[0][3] == 0.0f && m[1][3] == 0.0f && m[2][3] == 0.0f && m[3][3] == 1.0f; }
} // namespace detail

// Model-space matrix of every joint times its inverse bind matrix: what moves a vertex from its rest place. Each node's
// matrix is translation * rotation * scale built in one go (the rotation as glm::mat4_cast makes it, from a unit
// quaternion), and the products are affine ones.
inline void jointMatrices(const Skeleton &skeleton, const std::vector<NodePose> &pose, std::vector<glm::mat4> &globals,
                          std::vector<glm::mat4> &joints) {
  globals.resize(skeleton.nodes.size());
  for (int node : skeleton.order) {
    const NodePose &local = pose[node];
    const glm::quat &q = local.rotation;
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xz = q.x * q.z, xy = q.x * q.y, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    glm::mat4 matrix;
    matrix[0] = glm::vec4(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f) * local.scale.x;
    matrix[1] = glm::vec4(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f) * local.scale.y;
    matrix[2] = glm::vec4(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f) * local.scale.z;
    matrix[3] = glm::vec4(local.translation, 1.0f);
    const int parent = skeleton.nodes[node].parent;
    globals[node] = parent >= 0 ? detail::affineProduct(globals[parent], matrix) : matrix;
  }
  joints.resize(skeleton.jointNodes.size());
  for (size_t j = 0; j < joints.size(); ++j) {
    const glm::mat4 &global = globals[skeleton.jointNodes[j]];
    joints[j] = detail::isAffine(skeleton.inverseBind[j]) ? detail::affineProduct(global, skeleton.inverseBind[j])
                                                          : global * skeleton.inverseBind[j];
  }
}

} // namespace SkeletonPose
