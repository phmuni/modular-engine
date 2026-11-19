// Texture streaming: allocates every mip level, then fills them with glTexSubImage2D / glCompressedTexSubImage2D in
// row bands that fit the per-frame budget.

#include "rendering/resources/textureUploader.h"

#include <algorithm>
#include <deque>

namespace {

// Levels up to this many bytes in total are sent when the texture is created (a 256x256 RGBA chain is ~350 KB)
constexpr size_t kImmediateBytes = 512 * 1024;

struct Level {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> data;
};

struct Job {
  GLuint texture = 0;
  bool compressed = false;
  GLenum format = 0; // pixel format (GL_RGB...) or compressed format
  GLenum type = GL_UNSIGNED_BYTE;
  int channels = 0;
  std::vector<Level> levels; // level 0 first
  int level = 0;             // the level being sent; levels above it are complete
  int row = 0;               // next pixel row of that level (a multiple of 4 when compressed)
};

std::deque<Job> g_jobs;

constexpr GLenum kFormats[] = {GL_RED, GL_RG, GL_RGB, GL_RGBA};

size_t bytesPerComponent(GLenum type) {
  return type == GL_FLOAT ? 4 : type == GL_UNSIGNED_SHORT ? 2 : 1;
}

// Bytes of one band: a pixel row, or a row of 4x4 blocks when compressed
size_t bandBytes(const Job &job, const Level &level) {
  if (job.compressed)
    return static_cast<size_t>((level.width + 3) / 4) * (job.channels == 4 ? 16 : 8);
  return static_cast<size_t>(level.width) * job.channels * bytesPerComponent(job.type);
}
int bandHeight(const Job &job) { return job.compressed ? 4 : 1; }

// Sends rows of the current level until the level is done or `budget` runs out; returns the bytes sent
size_t sendRows(Job &job, size_t budget) {
  Level &level = job.levels[static_cast<size_t>(job.level)];
  const size_t perBand = bandBytes(job, level);
  const int bandRows = bandHeight(job);
  const int bandsLeft = (level.height - job.row + bandRows - 1) / bandRows;
  const int bands = std::clamp(static_cast<int>(budget / std::max<size_t>(perBand, 1)), 1, bandsLeft);
  const int rows = std::min(bands * bandRows, level.height - job.row);
  const uint8_t *data = level.data.data() + static_cast<size_t>(job.row / bandRows) * perBand;

  if (job.compressed)
    glCompressedTexSubImage2D(GL_TEXTURE_2D, job.level, 0, job.row, level.width, rows, job.format,
                              static_cast<GLsizei>(bands * perBand), data);
  else
    glTexSubImage2D(GL_TEXTURE_2D, job.level, 0, job.row, level.width, rows, job.format, job.type, data);
  job.row += rows;
  return static_cast<size_t>(bands) * perBand;
}

// Moves to the next (larger) level once one completes, exposing it to sampling; returns true when all are sent
bool finishLevels(Job &job) {
  while (job.level >= 0 && job.row >= job.levels[static_cast<size_t>(job.level)].height) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, job.level);
    std::vector<uint8_t>().swap(job.levels[static_cast<size_t>(job.level)].data); // sent; free the memory now
    --job.level;
    job.row = 0;
  }
  return job.level < 0;
}

GLuint magentaTexture() {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  const unsigned char color[3] = {255, 0, 255};
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, color);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  return texture;
}

} // namespace

namespace TextureUploader {

GLuint upload(DecodedImage image) {
  if (image.empty() || (!image.compressed() && (image.channels < 1 || image.channels > 4)))
    return magentaTexture();

  Job job;
  job.compressed = image.compressed();
  job.channels = image.channels;
  job.type = image.type;
  job.format = job.compressed ? image.compressedFormat : kFormats[image.channels - 1];

  int width = image.width, height = image.height;
  auto addLevel = [&](std::vector<uint8_t> &&data) {
    job.levels.push_back(Level{width, height, std::move(data)});
    width = std::max(width / 2, 1);
    height = std::max(height / 2, 1);
  };
  if (job.compressed) {
    for (auto &level : image.compressedLevels)
      addLevel(std::move(level));
  } else {
    addLevel(std::move(image.pixels));
    for (auto &level : image.mipmaps)
      addLevel(std::move(level));
  }
  const bool gpuMipmaps = !job.compressed && image.mipmaps.empty(); // images prepare() could not handle

  glGenTextures(1, &job.texture);
  glBindTexture(GL_TEXTURE_2D, job.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  // Rows are tightly packed; the default 4-byte alignment skews RGB images whose width * 3 is not a multiple of 4
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  if (gpuMipmaps) {
    const Level &level = job.levels[0];
    glTexImage2D(GL_TEXTURE_2D, 0, job.format, level.width, level.height, 0, job.format, job.type, level.data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    return job.texture;
  }

  // Storage for every level first; the data follows, smallest level first
  for (size_t i = 0; i < job.levels.size(); ++i) {
    const Level &level = job.levels[i];
    if (job.compressed)
      glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), job.format, level.width, level.height, 0,
                             static_cast<GLsizei>(level.data.size()), nullptr);
    else
      glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), job.format, level.width, level.height, 0, job.format,
                   job.type, nullptr);
  }
  const int last = static_cast<int>(job.levels.size()) - 1;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, last);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, last);
  job.level = last;

  // The small levels now, so the texture can be drawn this frame
  size_t sent = 0;
  while (job.level >= 0 && sent + job.levels[static_cast<size_t>(job.level)].data.size() <= kImmediateBytes) {
    sent += sendRows(job, job.levels[static_cast<size_t>(job.level)].data.size());
    finishLevels(job);
  }

  GLuint texture = job.texture;
  if (job.level >= 0)
    g_jobs.push_back(std::move(job));
  return texture;
}

void pump(size_t budgetBytes) {
  if (g_jobs.empty())
    return;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  size_t sent = 0;
  // One texture at a time, oldest first, so each sharpens as soon as possible
  while (!g_jobs.empty() && sent < budgetBytes) {
    Job &job = g_jobs.front();
    glBindTexture(GL_TEXTURE_2D, job.texture);
    sent += sendRows(job, budgetBytes - sent);
    if (finishLevels(job))
      g_jobs.pop_front();
  }
}

size_t pendingCount() { return g_jobs.size(); }

} // namespace TextureUploader
