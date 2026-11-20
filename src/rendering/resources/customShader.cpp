// Reading custom surface shader files: parameters, the two hook functions and the render mode pragma.

#include "rendering/resources/customShader.h"
#include <cstdlib>
#include <regex>

namespace {
// The text with comments blanked out (newlines kept), so positions match the original and searches skip comments
std::string withoutComments(const std::string &text) {
  std::string out = text;
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '/') {
      while (i < out.size() && out[i] != '\n')
        out[i++] = ' ';
    } else if (out[i] == '/' && i + 1 < out.size() && out[i + 1] == '*') {
      const size_t end = out.find("*/", i + 2);
      const size_t stop = end == std::string::npos ? out.size() : end + 2;
      for (; i < stop; ++i)
        if (out[i] != '\n')
          out[i] = ' ';
      --i;
    }
  }
  return out;
}

// The span of `void name(...) { ... }` in `code` (comments already blanked), or false
bool findFunction(const std::string &code, const char *name, size_t &begin, size_t &end) {
  const std::regex pattern(std::string("\\bvoid\\s+") + name + "\\s*\\(");
  std::smatch match;
  if (!std::regex_search(code, match, pattern))
    return false;
  begin = static_cast<size_t>(match.position(0));
  const size_t open = code.find('{', begin);
  if (open == std::string::npos)
    return false;
  int depth = 0;
  for (size_t i = open; i < code.size(); ++i) {
    if (code[i] == '{')
      ++depth;
    else if (code[i] == '}' && --depth == 0) {
      end = i + 1;
      return true;
    }
  }
  return false;
}

// `text` with [begin, end) removed but its newlines kept, so line numbers in compile errors still match the file
std::string eraseKeepingLines(std::string text, size_t begin, size_t end) {
  std::string kept;
  for (size_t i = begin; i < end; ++i)
    if (text[i] == '\n')
      kept += '\n';
  text.replace(begin, end - begin, kept);
  return text;
}

std::vector<float> numbersIn(const std::string &text) {
  std::vector<float> numbers;
  const char *p = text.c_str();
  while (*p) {
    char *stop = nullptr;
    const float value = std::strtof(p, &stop);
    if (stop != p) {
      numbers.push_back(value);
      p = stop;
    } else {
      ++p;
    }
  }
  return numbers;
}

glm::vec4 parseDefault(ShaderParamType type, std::string initializer) {
  glm::vec4 value(0.0f);
  const size_t open = initializer.find('(');
  if (type != ShaderParamType::Float && open != std::string::npos)
    initializer = initializer.substr(open + 1);
  const std::vector<float> numbers = numbersIn(initializer);
  if (numbers.empty())
    return value;
  const int count = type == ShaderParamType::Float  ? 1
                    : type == ShaderParamType::Vec2 ? 2
                    : type == ShaderParamType::Vec3 ? 3
                                                    : 4;
  for (int i = 0; i < count; ++i)
    value[i] = numbers.size() == 1 ? numbers[0] : (i < static_cast<int>(numbers.size()) ? numbers[i] : 0.0f);
  return value;
}
} // namespace

CustomShaderCode parseCustomShader(const std::string &text) {
  CustomShaderCode code;
  const std::string bare = withoutComments(text);

  static const std::regex uniformPattern(
      R"(\buniform\s+(float|vec2|vec3|vec4|sampler2D)\s+(\w+)\s*(?:=\s*([^;]*))?;)");
  for (auto it = std::sregex_iterator(bare.begin(), bare.end(), uniformPattern); it != std::sregex_iterator(); ++it) {
    const std::string type = (*it)[1];
    ShaderParamDecl decl;
    decl.name = (*it)[2];
    decl.type = type == "float"  ? ShaderParamType::Float
                : type == "vec2" ? ShaderParamType::Vec2
                : type == "vec3" ? ShaderParamType::Vec3
                : type == "vec4" ? ShaderParamType::Vec4
                                 : ShaderParamType::Texture;
    if (decl.name == "time")
      continue; // the engine's own
    if (decl.type != ShaderParamType::Texture && (*it)[3].matched)
      decl.defaultValue = parseDefault(decl.type, (*it)[3]);
    code.params.push_back(std::move(decl));
  }

  static const std::regex blendPattern(R"((^|\n)[ \t]*#[ \t]*pragma[ \t]+blend\b)");
  code.blend = std::regex_search(bare, blendPattern);
  static const std::regex noShadowPattern(R"((^|\n)[ \t]*#[ \t]*pragma[ \t]+noshadow\b)");
  code.castsShadow = !std::regex_search(bare, noShadowPattern);

  size_t surfaceBegin = 0, surfaceEnd = 0, vertexBegin = 0, vertexEnd = 0;
  code.hasSurface = findFunction(bare, "surface", surfaceBegin, surfaceEnd);
  code.hasVertex = findFunction(bare, "vertex", vertexBegin, vertexEnd);
  if (!code.hasSurface && !code.hasVertex) {
    code.error = "it defines neither `void surface(inout Surface s)` nor `void vertex(inout VertexData v)`";
    return code;
  }

  // Each stage gets the file without the other stage's function: it may use things only that stage has
  code.vertexStage = code.hasSurface ? eraseKeepingLines(text, surfaceBegin, surfaceEnd) : text;
  code.fragmentStage = code.hasVertex ? eraseKeepingLines(text, vertexBegin, vertexEnd) : text;
  static const std::regex cutBelowPattern(R"(\bcutBelow\s*\()");
  code.cutout = !code.blend && std::regex_search(withoutComments(code.fragmentStage), cutBelowPattern);
  return code;
}

glm::vec4 shaderValueAsVec4(const ShaderValue &value) {
  if (const float *number = std::get_if<float>(&value))
    return glm::vec4(*number);
  if (const glm::vec2 *v = std::get_if<glm::vec2>(&value))
    return glm::vec4(*v, 0.0f, 0.0f);
  if (const glm::vec3 *v = std::get_if<glm::vec3>(&value))
    return glm::vec4(*v, 0.0f);
  if (const glm::vec4 *v = std::get_if<glm::vec4>(&value))
    return *v;
  return glm::vec4(0.0f);
}

bool shaderValueFits(const ShaderValue &value, ShaderParamType type) {
  if (type == ShaderParamType::Texture)
    return std::holds_alternative<std::string>(value);
  if (std::holds_alternative<std::string>(value))
    return false;
  if (std::holds_alternative<float>(value))
    return true;
  switch (type) {
  case ShaderParamType::Float: return false;
  case ShaderParamType::Vec2: return std::holds_alternative<glm::vec2>(value);
  case ShaderParamType::Vec3: return std::holds_alternative<glm::vec3>(value);
  case ShaderParamType::Vec4: return std::holds_alternative<glm::vec4>(value);
  case ShaderParamType::Texture: break;
  }
  return false;
}

ShaderValue shaderValueAs(const ShaderValue &value, ShaderParamType type) {
  const glm::vec4 v = shaderValueAsVec4(value);
  switch (type) {
  case ShaderParamType::Float: return v.x;
  case ShaderParamType::Vec2: return glm::vec2(v);
  case ShaderParamType::Vec3: return glm::vec3(v);
  case ShaderParamType::Vec4: return v;
  case ShaderParamType::Texture: break;
  }
  return value;
}

ShaderValue shaderParamDefault(const ShaderParamDecl &decl) {
  if (decl.type == ShaderParamType::Texture)
    return std::string();
  return shaderValueAs(decl.defaultValue, decl.type);
}
