#pragma once
// Custom surface shaders: a small GLSL file with a `surface` and/or `vertex` function that the engine injects into its
// standard lit shader (see parseCustomShader for the file format, and the README for what the functions receive).
// This header is the part without OpenGL: reading the file, and the values a material gives its uniforms.

#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <vector>

enum class ShaderParamType { Float, Vec2, Vec3, Vec4, Texture };

// What a material gives one of its shader's uniforms; a texture is an asset path
using ShaderValue = std::variant<float, glm::vec2, glm::vec3, glm::vec4, std::string>;

// A uniform the file declares (`uniform vec3 tint = vec3(1.0);`); sampler2D uniforms are textures
struct ShaderParamDecl {
  std::string name;
  ShaderParamType type = ShaderParamType::Float;
  glm::vec4 defaultValue{0.0f}; // from the declaration's initializer, zero without one (unused for textures)
};

// A custom shader file, read: the code each stage gets and what the editor and materials need to know about it
struct CustomShaderCode {
  std::vector<ShaderParamDecl> params;
  std::string vertexStage;   // the file without surface(): inserted into the vertex shader
  std::string fragmentStage; // the file without vertex(): inserted into the fragment shader
  bool hasSurface = false;
  bool hasVertex = false;
  bool blend = false; // `#pragma blend`: parts using it are drawn in the transparent pass
  bool cutout = false;     // the fragment stage calls cutBelow(): its draws use alpha-to-coverage when MSAA is on
  bool castsShadow = true; // false with `#pragma noshadow`: parts using it cast no shadow
  std::string error;  // why the file cannot be used (empty when it can)
};

// The file's text is GLSL 330 with these additions, all optional but at least one of the two functions:
//   uniform float speed = 1.0;            // parameters: float, vec2, vec3, vec4 and sampler2D, one per declaration
//   void surface(inout Surface s) { }     // after base color and textures, before lighting
//   void vertex(inout VertexData v) { }   // moves the vertex (world space) before projection
//   cutBelow(value, threshold);           // in surface(): cuts the surface away where value < threshold; a discard, or a
//                                         // smooth edge with MSAA (alpha-to-coverage). Prefer it to `discard`.
//   #pragma blend                         // draw with alpha blending (the transparent pass)
//   #pragma noshadow                      // the part casts no shadow (otherwise vertex() and discard show in it)
// `time` (seconds) is provided by the engine; do not declare it.
CustomShaderCode parseCustomShader(const std::string &text);

// The value as four numbers, whatever its type (a float fills them all): what a uniform of any type gets from it
glm::vec4 shaderValueAsVec4(const ShaderValue &value);
// Whether the value can be given to a uniform of this type (a number fills a vector; a texture is a path)
bool shaderValueFits(const ShaderValue &value, ShaderParamType type);
// `value` as the declared type: a float given to a vec3 fills it
ShaderValue shaderValueAs(const ShaderValue &value, ShaderParamType type);
// The declaration's default as a value (a texture's: an empty path)
ShaderValue shaderParamDefault(const ShaderParamDecl &decl);
