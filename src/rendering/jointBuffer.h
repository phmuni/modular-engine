#pragma once
// The joint matrices of every skinned model drawn in a frame, in one buffer texture the vertex shaders read
// (samplerBuffer jointData). With them all in one place a skinned model is drawn instanced like any other mesh, each
// instance carrying the index of its first joint (InstanceData::jointOffset).

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

// Texture unit of the joint buffer. Units 0-15 are the fragment stage's (see the units in renderSystem.cpp); a vertex
// shader has units of its own, and the combined limit of any GL 4.1 driver is at least 48.
constexpr int kJointBufferUnit = 16;

// Points the program's `jointData` sampler (if it has one) at kJointBufferUnit. Done when a program is linked: a
// samplerBuffer left on unit 0 next to a sampler of another type makes every draw of the program an error.
void bindJointSampler(GLuint program);

class JointBuffer {
public:
  // One joint is three RGBA32F texels: the three rows of its affine matrix
  static constexpr int kTexelsPerJoint = 3;

  void init();
  void shutdown();

  // How many joints fit: the driver's GL_MAX_TEXTURE_BUFFER_SIZE, or the limit set with setMaxJoints
  int maxJoints() const { return m_limit > 0 && m_limit < m_driverMax ? m_limit : m_driverMax; }
  // Lowers the limit below the driver's (for tests); 0 goes back to the driver's
  void setMaxJoints(int joints) { m_limit = joints; }

  // Appends a joint's rows for upload; `texels` holds kTexelsPerJoint entries per joint of this frame
  static void appendJoint(std::vector<glm::vec4> &texels, const glm::mat4 &matrix);

  // Replaces the contents with `jointCount` joints and binds the buffer texture to kJointBufferUnit. The previous
  // contents are orphaned, so a frame still being drawn is not stalled.
  void upload(const std::vector<glm::vec4> &texels);
  int uploadedJoints() const { return m_uploaded; }

private:
  GLuint m_buffer = 0;
  GLuint m_texture = 0;
  int m_capacity = 0; // joints the buffer store holds
  int m_uploaded = 0;
  int m_driverMax = 0;
  int m_limit = 0;

  void bind(bool attach) const;
};
