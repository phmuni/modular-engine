// Mip generation, S3TC compression (stb_dxt) and the compressed texture cache.

#include "rendering/resources/textureProcessing.h"

#include "foundation/core/config.h"
#include "foundation/core/fileCache.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <stb_dxt/stb_dxt.h>
#include <thread>

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RG_RGTC2
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#endif

namespace {

std::atomic<bool> g_s3tc{false};

constexpr char kMagic[8] = {'T', 'E', 'X', 'C', 'A', 'C', 'H', 'E'};
// Bump when the encoder, mip filter or layout changes, so textures are converted again
constexpr uint32_t kVersion = 2;

// Halves an 8-bit image with a 2x2 box filter (a 1-pixel side stays 1)
std::vector<uint8_t> halve(const std::vector<uint8_t> &source, int width, int height, int channels, int &outWidth,
                           int &outHeight) {
  outWidth = std::max(width / 2, 1);
  outHeight = std::max(height / 2, 1);
  std::vector<uint8_t> result(static_cast<size_t>(outWidth) * outHeight * channels);
  for (int y = 0; y < outHeight; ++y) {
    const int y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
    for (int x = 0; x < outWidth; ++x) {
      const int x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
      for (int c = 0; c < channels; ++c) {
        auto at = [&](int px, int py) { return source[(static_cast<size_t>(py) * width + px) * channels + c]; };
        const int sum = at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1);
        result[(static_cast<size_t>(y) * outWidth + x) * channels + c] = static_cast<uint8_t>((sum + 2) / 4);
      }
    }
  }
  return result;
}

// One level to BC1 (RGB, 8 bytes per 4x4 block), BC3 (RGBA, 16 bytes) or BC5 (the red and green channels, 16 bytes);
// edge blocks repeat the last row/column. Rows of blocks are split across threads: an 8K texture is 4 million blocks.
std::vector<uint8_t> compressLevel(const std::vector<uint8_t> &pixels, int width, int height, int channels,
                                   GLenum format) {
  const bool alpha = format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
  const bool twoChannels = format == GL_COMPRESSED_RG_RGTC2;
  const size_t blockBytes = format == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ? 8 : 16;
  const int blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
  std::vector<uint8_t> output(static_cast<size_t>(blocksX) * blocksY * blockBytes);

  auto compressRows = [&](int firstRow, int lastRow) {
    unsigned char block[64];
    for (int by = firstRow; by < lastRow; ++by) {
      for (int bx = 0; bx < blocksX; ++bx) {
        const int stride = twoChannels ? 2 : 4;
        for (int py = 0; py < 4; ++py) {
          const int y = std::min(by * 4 + py, height - 1);
          for (int px = 0; px < 4; ++px) {
            const int x = std::min(bx * 4 + px, width - 1);
            const uint8_t *source = &pixels[(static_cast<size_t>(y) * width + x) * channels];
            unsigned char *target = &block[(py * 4 + px) * stride];
            target[0] = source[0];
            target[1] = source[1];
            if (!twoChannels) {
              target[2] = source[2];
              target[3] = alpha ? source[3] : 255;
            }
          }
        }
        unsigned char *destination = &output[(static_cast<size_t>(by) * blocksX + bx) * blockBytes];
        if (twoChannels)
          stb_compress_bc5_block(destination, block);
        else
          stb_compress_dxt_block(destination, block, alpha ? 1 : 0, STB_DXT_NORMAL);
      }
    }
  };

  const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, blocksY);
  if (threads == 1 || blocksY < 64) {
    compressRows(0, blocksY);
    return output;
  }
  std::vector<std::future<void>> workers;
  const int rowsPerThread = (blocksY + threads - 1) / threads;
  for (int first = 0; first < blocksY; first += rowsPerThread)
    workers.push_back(std::async(std::launch::async, compressRows, first, std::min(first + rowsPerThread, blocksY)));
  for (auto &worker : workers)
    worker.get();
  return output;
}

std::string entryPath(const std::string &key) { return FileCache::entryPath("textures", key, ".tex"); }

bool readCached(const std::string &key, const std::string &sourceFile, DecodedImage &image) {
  FileCache::FileStamp current;
  if (!FileCache::stampOf(sourceFile, current))
    return false;
  FileCache::Reader reader(entryPath(key));
  if (!reader)
    return false;

  char magic[8];
  uint32_t version = 0;
  FileCache::FileStamp stored;
  std::string storedKey;
  int32_t width = 0, height = 0, channels = 0;
  uint32_t format = 0, levels = 0;
  if (!reader.value(magic) || std::memcmp(magic, kMagic, sizeof(magic)) != 0 || !reader.value(version) ||
      version != kVersion || !reader.value(stored) || !(stored == current) || !reader.string(storedKey) ||
      storedKey != key || !reader.value(width) || !reader.value(height) || !reader.value(channels) ||
      !reader.value(format) || !reader.value(levels) || levels == 0 || levels > 32)
    return false;

  DecodedImage loaded;
  loaded.width = width;
  loaded.height = height;
  loaded.channels = channels;
  loaded.compressedFormat = format;
  loaded.compressedLevels.resize(levels);
  for (auto &level : loaded.compressedLevels)
    if (!reader.array(level))
      return false;
  image = std::move(loaded);
  return true;
}

void writeCached(const std::string &key, const std::string &sourceFile, const DecodedImage &image) {
  FileCache::FileStamp stamp;
  if (!image.compressed() || !FileCache::stampOf(sourceFile, stamp))
    return;
  FileCache::Writer writer(entryPath(key));
  writer.value(kMagic);
  writer.value(kVersion);
  writer.value(stamp);
  writer.string(key); // guards against two keys sharing a hash
  writer.value(static_cast<int32_t>(image.width));
  writer.value(static_cast<int32_t>(image.height));
  writer.value(static_cast<int32_t>(image.channels));
  writer.value(static_cast<uint32_t>(image.compressedFormat));
  writer.value(static_cast<uint32_t>(image.compressedLevels.size()));
  for (const auto &level : image.compressedLevels)
    writer.array(level);
  if (!writer.commit())
    LOG_W("[Texture] Could not cache %s", key.c_str());
}

} // namespace

namespace TextureProcessing {

void detectSupport() {
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  bool found = false;
  for (GLint i = 0; i < count && !found; ++i) {
    const char *name = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
    found = name && std::strcmp(name, "GL_EXT_texture_compression_s3tc") == 0;
  }
  g_s3tc = found && EngineConfig::COMPRESS_TEXTURES;
  if (EngineConfig::COMPRESS_TEXTURES && !found)
    LOG_I("[Texture] The GPU has no S3TC; textures stay uncompressed");
}

namespace {
// The format an image is compressed to, or 0 to leave it uncompressed
GLenum compressedFormatFor(const DecodedImage &image) {
  if (!EngineConfig::COMPRESS_TEXTURES || (image.channels != 3 && image.channels != 4))
    return 0;
  if (image.kind == TextureKind::Normal)
    return GL_COMPRESSED_RG_RGTC2; // RGTC is core OpenGL 3.0
  if (!g_s3tc)
    return 0;
  return image.channels == 4 ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
}

std::string cacheKey(const std::string &key, TextureKind kind) {
  return kind == TextureKind::Normal ? key + "#normal" : key;
}
} // namespace

void prepare(DecodedImage &image) {
  if (image.empty() || image.compressed() || image.type != GL_UNSIGNED_BYTE || image.channels < 1 ||
      image.channels > 4)
    return;

  image.mipmaps.clear();
  int width = image.width, height = image.height;
  const std::vector<uint8_t> *previous = &image.pixels;
  while (width > 1 || height > 1) {
    int nextWidth = 0, nextHeight = 0;
    image.mipmaps.push_back(halve(*previous, width, height, image.channels, nextWidth, nextHeight));
    previous = &image.mipmaps.back();
    width = nextWidth;
    height = nextHeight;
  }

  const GLenum format = compressedFormatFor(image);
  if (!format)
    return;

  image.compressedFormat = format;
  width = image.width;
  height = image.height;
  image.compressedLevels.push_back(compressLevel(image.pixels, width, height, image.channels, format));
  for (const auto &mip : image.mipmaps) {
    width = std::max(width / 2, 1);
    height = std::max(height / 2, 1);
    image.compressedLevels.push_back(compressLevel(mip, width, height, image.channels, format));
  }
  image.pixels = {};
  image.mipmaps = {};
}

bool loadCached(const std::string &key, const std::string &sourceFile, TextureKind kind, DecodedImage &image) {
  if (!readCached(cacheKey(key, kind), sourceFile, image))
    return false;
  image.kind = kind;
  return true;
}

DecodedImage load(const std::string &key, const std::string &sourceFile, TextureKind kind,
                  const std::function<DecodedImage()> &decode) {
  DecodedImage image;
  if (loadCached(key, sourceFile, kind, image))
    return image;

  image = decode();
  image.kind = kind;
  // Normals need three channels; a grayscale "normal map" is a height map, which would light as tilted garbage
  if (kind == TextureKind::Normal && !image.empty() && image.channels < 3) {
    LOG_W("[Texture] %s is grayscale, probably a height map; it is not used as a normal map", key.c_str());
    DecodedImage none;
    none.kind = kind;
    return none;
  }
  prepare(image);
  if (image.compressed())
    writeCached(cacheKey(key, kind), sourceFile, image);
  return image;
}

} // namespace TextureProcessing
