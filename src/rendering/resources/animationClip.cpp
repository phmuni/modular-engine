// Preparing the channels of an animation clip when they are added, so that sampling has the least to do.

#include "rendering/resources/mesh.h"
#include <algorithm>
#include <cmath>

namespace {
// Quaternions this close (cosine of half the angle between them) are mixed by a normalized lerp: its path differs from
// the slerp's by under 1e-6 here
constexpr float kLerpCosine = 0.9999f;

glm::vec4 unit(const glm::vec4 &v) {
  const glm::quat q = glm::normalize(glm::quat(v.w, v.x, v.y, v.z));
  return {q.x, q.y, q.z, q.w};
}
} // namespace

void AnimationClip::addChannel(AnimationChannel channel, std::vector<float> times) {
  duration = std::max(duration, times.back());
  if (channel.interpolation != AnimationChannel::Interpolation::CubicSpline && channel.values.size() >= times.size()) {
    bool constant = true;
    for (size_t k = 1; k < times.size() && constant; ++k)
      constant = channel.values[k] == channel.values[0];
    if (constant) {
      const glm::vec4 &value = channel.values[0];
      constants.push_back({channel.node, channel.path,
                           channel.path == AnimationChannel::Path::Rotation ? unit(value) : value});
      return;
    }
  }
  if (channel.interpolation != AnimationChannel::Interpolation::CubicSpline &&
      channel.path == AnimationChannel::Path::Rotation) {
    for (glm::vec4 &value : channel.values)
      value = unit(value);
    if (channel.interpolation == AnimationChannel::Interpolation::Linear) {
      const size_t keys = std::min(times.size(), channel.values.size());
      channel.arcs.assign(keys, glm::vec3(0.0f));
      for (size_t k = 0; k + 1 < keys; ++k) {
        // The short way: a neighbour on the far side of the sphere stands for the same rotation as its opposite
        if (glm::dot(channel.values[k], channel.values[k + 1]) < 0.0f)
          channel.values[k + 1] = -channel.values[k + 1];
        const float cosine = std::min(glm::dot(channel.values[k], channel.values[k + 1]), 1.0f);
        if (cosine < kLerpCosine) {
          const float angle = std::acos(cosine);
          channel.arcs[k] = {angle, 1.0f / std::sin(angle), std::cos(angle) / std::sin(angle)};
        }
      }
    }
  }

  size_t found = tracks.size();
  for (size_t t = 0; t < tracks.size() && found == tracks.size(); ++t)
    if (tracks[t].times == times)
      found = t;
  if (found == tracks.size()) {
    AnimationTrack track;
    track.firstChannel = channels.size();
    track.times = times;
    tracks.push_back(std::move(track));
  }
  channel.track = static_cast<int>(found);
  const size_t at = tracks[found].firstChannel + tracks[found].channelCount;
  channels.insert(channels.begin() + at, std::move(channel));
  ++tracks[found].channelCount;
  for (size_t t = 0; t < tracks.size(); ++t)
    if (t != found && tracks[t].firstChannel >= at)
      ++tracks[t].firstChannel;
}
