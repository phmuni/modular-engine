// Generates assets/textures/skybox/space.png: an equirectangular (360 x 180 degree) panorama of stars and nebulae.
//
// Everything is computed from the view direction (3D noise on the unit sphere), so the image has no seam at the
// left/right edge and no pinching at the poles.
//
//   cc -O2 -o /tmp/generateSpaceSkybox tools/generateSpaceSkybox.c -lz -lm
//   /tmp/generateSpaceSkybox assets/textures/skybox/space.png [width]

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define PI 3.14159265358979f

typedef struct {
  float x, y, z;
} vec3;

static vec3 v3(float x, float y, float z) { return (vec3){x, y, z}; }
static vec3 add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static vec3 scale(vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static vec3 normalize(vec3 a) { return scale(a, 1.0f / sqrtf(dot(a, a))); }
static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static float smoothstepf(float a, float b, float x) {
  float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
static vec3 mixv(vec3 a, vec3 b, float t) { return add(scale(a, 1.0f - t), scale(b, t)); }

// Deterministic random numbers (the image is the same on every run)
static uint32_t rngState = 0x9E3779B9u;
static float randf(void) {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return (rngState & 0xFFFFFF) / 16777216.0f;
}

static float hash3(int x, int y, int z) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xFFFFFF) / 16777216.0f;
}

static float valueNoise(vec3 p) {
  int ix = (int)floorf(p.x), iy = (int)floorf(p.y), iz = (int)floorf(p.z);
  float fx = p.x - ix, fy = p.y - iy, fz = p.z - iz;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  fz = fz * fz * (3 - 2 * fz);
  float c[2][2][2];
  for (int a = 0; a < 2; ++a)
    for (int b = 0; b < 2; ++b)
      for (int d = 0; d < 2; ++d)
        c[a][b][d] = hash3(ix + a, iy + b, iz + d);
  float x00 = c[0][0][0] + (c[1][0][0] - c[0][0][0]) * fx, x10 = c[0][1][0] + (c[1][1][0] - c[0][1][0]) * fx;
  float x01 = c[0][0][1] + (c[1][0][1] - c[0][0][1]) * fx, x11 = c[0][1][1] + (c[1][1][1] - c[0][1][1]) * fx;
  float y0 = x00 + (x10 - x00) * fy, y1 = x01 + (x11 - x01) * fy;
  return y0 + (y1 - y0) * fz;
}

static float fbm(vec3 p, int octaves) {
  float sum = 0.0f, amplitude = 0.5f, norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += valueNoise(p) * amplitude;
    norm += amplitude;
    p = add(scale(p, 2.03f), v3(1.7f, 9.2f, 3.1f));
    amplitude *= 0.5f;
  }
  return sum / norm;
}

static vec3 directionOf(int width, int height, float px, float py) {
  float u = px / width, v = py / height; // v = 0 at the top row
  float longitude = (u - 0.5f) * 2.0f * PI;
  float latitude = (0.5f - v) * PI;
  // Longitude 0 looks down -Z, matching the engine's skybox shader
  return v3(cosf(latitude) * sinf(longitude), sinf(latitude), -cosf(latitude) * cosf(longitude));
}

static void writeChunk(FILE *file, const char *type, const unsigned char *data, uint32_t length) {
  unsigned char header[8] = {length >> 24, length >> 16, length >> 8, length, type[0], type[1], type[2], type[3]};
  fwrite(header, 1, 8, file);
  if (length)
    fwrite(data, 1, length, file);
  uLong crc = crc32(0, (const Bytef *)type, 4);
  crc = crc32(crc, data, length);
  unsigned char tail[4] = {crc >> 24, crc >> 16, crc >> 8, crc};
  fwrite(tail, 1, 4, file);
}

static int writePng(const char *path, const unsigned char *rgb, int width, int height) {
  size_t rowBytes = (size_t)width * 3 + 1;
  unsigned char *raw = malloc(rowBytes * height);
  for (int y = 0; y < height; ++y) {
    raw[y * rowBytes] = 0; // no filter
    memcpy(raw + y * rowBytes + 1, rgb + (size_t)y * width * 3, (size_t)width * 3);
  }
  uLongf packedSize = compressBound(rowBytes * height);
  unsigned char *packed = malloc(packedSize);
  compress2(packed, &packedSize, raw, rowBytes * height, 9);

  FILE *file = fopen(path, "wb");
  if (!file)
    return 0;
  static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(signature, 1, 8, file);
  unsigned char ihdr[13] = {width >> 24, width >> 16, width >> 8, width, height >> 24, height >> 16, height >> 8,
                            height, 8, 2, 0, 0, 0};
  writeChunk(file, "IHDR", ihdr, 13);
  writeChunk(file, "IDAT", packed, (uint32_t)packedSize);
  writeChunk(file, "IEND", NULL, 0);
  fclose(file);
  free(raw);
  free(packed);
  return 1;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s output.png [width]\n", argv[0]);
    return 1;
  }
  const int width = argc > 2 ? atoi(argv[2]) : 4096;
  const int height = width / 2;
  float *image = calloc((size_t)width * height * 3, sizeof(float));

  // The galactic band: a great circle tilted against the horizon
  const vec3 bandNormal = normalize(v3(0.25f, 0.9f, 0.35f));

  // Nebulae and the band's glow, per pixel
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      vec3 d = directionOf(width, height, x + 0.5f, y + 0.5f);
      vec3 color = v3(0.002f, 0.0025f, 0.006f); // not quite black

      float band = expf(-powf(dot(d, bandNormal) / 0.22f, 2.0f));
      float dust = fbm(scale(d, 3.0f), 6);
      float glow = band * smoothstepf(0.35f, 0.8f, dust);
      color = add(color, scale(v3(0.10f, 0.085f, 0.12f), glow));

      // Two nebula clouds in different hues, shaped by warped noise
      vec3 warp = v3(fbm(scale(d, 2.0f), 4), fbm(add(scale(d, 2.0f), v3(5.2f, 1.3f, 2.8f)), 4), 0.0f);
      vec3 q = add(scale(d, 1.6f), scale(v3(warp.x, warp.y, warp.x - warp.y), 1.8f));
      float purple = smoothstepf(0.52f, 0.85f, fbm(q, 6));
      float teal = smoothstepf(0.55f, 0.88f, fbm(add(q, v3(7.1f, 3.3f, 1.9f)), 6));
      // Concentrated on one side of the sky, so not every direction looks the same
      float purpleRegion = smoothstepf(-0.2f, 0.9f, dot(d, normalize(v3(-0.6f, 0.25f, -0.75f))));
      float tealRegion = smoothstepf(-0.1f, 0.9f, dot(d, normalize(v3(0.8f, -0.15f, -0.4f))));
      color = add(color, scale(mixv(v3(0.22f, 0.05f, 0.30f), v3(0.55f, 0.18f, 0.45f), purple), purple * purpleRegion * 0.55f));
      color = add(color, scale(mixv(v3(0.02f, 0.14f, 0.22f), v3(0.10f, 0.40f, 0.50f), teal), teal * tealRegion * 0.45f));

      // Dark dust lanes cut through the clouds
      float lanes = smoothstepf(0.45f, 0.7f, fbm(add(scale(d, 4.5f), v3(2.0f, 2.0f, 2.0f)), 5));
      color = scale(color, 1.0f - 0.6f * lanes);

      float *pixel = image + ((size_t)y * width + x) * 3;
      pixel[0] = color.x;
      pixel[1] = color.y;
      pixel[2] = color.z;
    }
  }

  // Stars: more of them near the band, a few bright ones. Each is splatted as a small gaussian over the pixels it
  // covers, measured as an angle so stars near the poles stay round.
  const float pixelAngle = 2.0f * PI / width;
  const int starCount = width * 6;
  for (int i = 0; i < starCount; ++i) {
    vec3 d = normalize(v3(randf() * 2 - 1, randf() * 2 - 1, randf() * 2 - 1));
    float band = expf(-powf(dot(d, bandNormal) / 0.3f, 2.0f));
    if (randf() > 0.35f + 0.65f * band)
      continue;

    float r = randf();
    float brightness = 0.06f + 2.4f * powf(r, 20.0f) + 0.5f * powf(r, 4.0f);
    float temperature = randf();
    vec3 tint = temperature < 0.15f ? v3(1.0f, 0.75f, 0.55f) : (temperature > 0.8f ? v3(0.7f, 0.82f, 1.0f) : v3(1, 1, 1));
    float radius = pixelAngle * (0.45f + 0.7f * powf(r, 12.0f));

    float latitude = asinf(clampf(d.y, -1, 1));
    float longitude = atan2f(d.x, -d.z);
    float cx = (longitude / (2 * PI) + 0.5f) * width;
    float cy = (0.5f - latitude / PI) * height;
    int reachY = (int)ceilf(3.0f * radius / pixelAngle) + 1;
    int reachX = (int)ceilf(reachY / fmaxf(cosf(latitude), 0.02f));
    if (reachX > width / 2)
      reachX = width / 2;

    for (int dy = -reachY; dy <= reachY; ++dy) {
      int py = (int)cy + dy;
      if (py < 0 || py >= height)
        continue;
      for (int dx = -reachX; dx <= reachX; ++dx) {
        int px = (((int)cx + dx) % width + width) % width;
        vec3 p = directionOf(width, height, px + 0.5f, py + 0.5f);
        float angle = acosf(clampf(dot(p, d), -1, 1));
        float weight = expf(-(angle * angle) / (2 * radius * radius));
        if (weight < 0.003f)
          continue;
        float *pixel = image + ((size_t)py * width + px) * 3;
        pixel[0] += tint.x * brightness * weight;
        pixel[1] += tint.y * brightness * weight;
        pixel[2] += tint.z * brightness * weight;
      }
    }
  }

  // To 8-bit sRGB-ish values (the engine treats color textures as sRGB)
  unsigned char *rgb = malloc((size_t)width * height * 3);
  for (size_t i = 0; i < (size_t)width * height * 3; ++i) {
    float value = image[i] / (1.0f + image[i] * 0.15f); // soft roll-off of the brightest stars
    rgb[i] = (unsigned char)(clampf(powf(value, 1.0f / 2.2f), 0, 1) * 255.0f + 0.5f);
  }
  int ok = writePng(argv[1], rgb, width, height);
  free(image);
  free(rgb);
  if (!ok) {
    fprintf(stderr, "could not write %s\n", argv[1]);
    return 1;
  }
  printf("wrote %s (%dx%d)\n", argv[1], width, height);
  return 0;
}
