// The joint buffer texture (see jointBuffer.h).

#include "rendering/jointBuffer.h"
#include <algorithm>

void bindJointSampler(GLuint program) {
  const GLint location = glGetUniformLocation(program, "jointData");
  if (location == -1)
    return;
  GLint previous = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
  glUseProgram(program);
  glUniform1i(location, kJointBufferUnit);
  glUseProgram(static_cast<GLuint>(previous));
}

void JointBuffer::init() {
  GLint maxTexels = 0;
  glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels);
  m_driverMax = std::max(maxTexels / kTexelsPerJoint, 1);
  glGenBuffers(1, &m_buffer);
  glGenTextures(1, &m_texture);
  // A texture is bound from the start, so a skinned program's draw never finds the unit empty; one identity joint
  upload({glm::vec4(1, 0, 0, 0), glm::vec4(0, 1, 0, 0), glm::vec4(0, 0, 1, 0)});
}

void JointBuffer::shutdown() {
  if (m_texture)
    glDeleteTextures(1, &m_texture);
  if (m_buffer)
    glDeleteBuffers(1, &m_buffer);
  m_texture = m_buffer = 0;
  m_capacity = 0;
}

void JointBuffer::appendJoint(std::vector<glm::vec4> &texels, const glm::mat4 &m) {
  // glm is column-major: row r of the matrix is (m[0][r], m[1][r], m[2][r], m[3][r]). The last row of an affine
  // matrix is (0, 0, 0, 1) and is not stored.
  for (int row = 0; row < 3; ++row)
    texels.emplace_back(m[0][row], m[1][row], m[2][row], m[3][row]);
}

// Binds the texture to its unit, attaching it to the buffer's store when asked (after the store was made again)
void JointBuffer::bind(bool attach) const {
  GLint previous = GL_TEXTURE0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previous);
  glActiveTexture(GL_TEXTURE0 + kJointBufferUnit);
  glBindTexture(GL_TEXTURE_BUFFER, m_texture);
  if (attach)
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_buffer);
  glActiveTexture(static_cast<GLenum>(previous));
}

void JointBuffer::upload(const std::vector<glm::vec4> &texels) {
  const int joints = static_cast<int>(texels.size()) / kTexelsPerJoint;
  m_uploaded = joints;
  glBindBuffer(GL_TEXTURE_BUFFER, m_buffer);
  if (joints > m_capacity) {
    // Grows by doubling, and the texture is attached again to the new store
    m_capacity = std::min(std::max({joints, m_capacity * 2, 16}), std::max(m_driverMax, joints));
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(m_capacity) * kTexelsPerJoint * sizeof(glm::vec4), nullptr,
                 GL_STREAM_DRAW);
    bind(true);
  } else {
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(m_capacity) * kTexelsPerJoint * sizeof(glm::vec4), nullptr,
                 GL_STREAM_DRAW);
    bind(false);
  }
  if (!texels.empty())
    glBufferSubData(GL_TEXTURE_BUFFER, 0, static_cast<GLsizeiptr>(texels.size() * sizeof(glm::vec4)), texels.data());
  glBindBuffer(GL_TEXTURE_BUFFER, 0);
}
