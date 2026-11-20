// Reading animation state machine definitions.

#include "systems/animationGraph.h"

#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int AnimationGraph::findState(const std::string &name) const {
  for (size_t i = 0; i < states.size(); ++i)
    if (states[i].name == name)
      return static_cast<int>(i);
  return -1;
}

namespace {
bool parseOp(const std::string &text, AnimationGraph::Op &op) {
  static const std::map<std::string, AnimationGraph::Op> ops = {
      {">", AnimationGraph::Op::Greater},       {"<", AnimationGraph::Op::Less},
      {">=", AnimationGraph::Op::GreaterEqual}, {"<=", AnimationGraph::Op::LessEqual},
      {"==", AnimationGraph::Op::Equal},        {"!=", AnimationGraph::Op::NotEqual},
      {"true", AnimationGraph::Op::IsTrue},     {"false", AnimationGraph::Op::IsFalse},
      {"trigger", AnimationGraph::Op::Triggered}};
  auto it = ops.find(text);
  if (it == ops.end())
    return false;
  op = it->second;
  return true;
}

float numberOf(const json &value, float fallback) {
  if (value.is_boolean())
    return value.get<bool>() ? 1.0f : 0.0f;
  return value.is_number() ? value.get<float>() : fallback;
}

std::string textOf(const json &object, const char *key, const std::string &fallback = std::string()) {
  if (!object.is_object())
    return fallback;
  auto it = object.find(key);
  return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

float floatOf(const json &object, const char *key, float fallback) {
  if (!object.is_object())
    return fallback;
  auto it = object.find(key);
  return it != object.end() ? numberOf(*it, fallback) : fallback;
}
} // namespace

bool AnimationGraph::parse(const json &data, AnimationGraph &graph, std::string &error) {
  graph = AnimationGraph();
  if (!data.is_object()) {
    error = "not a JSON object";
    return false;
  }

  if (data.contains("params")) {
    if (!data["params"].is_object()) {
      error = "\"params\" must be an object";
      return false;
    }
    for (const auto &[name, spec] : data["params"].items()) {
      Param param;
      // "speed": "float" is the short form of {"type": "float"}
      const std::string type = spec.is_string() ? spec.get<std::string>() : textOf(spec, "type", "float");
      if (type == "float")
        param.type = ParamType::Float;
      else if (type == "bool")
        param.type = ParamType::Bool;
      else if (type == "trigger")
        param.type = ParamType::Trigger;
      else {
        error = "parameter \"" + name + "\" has type \"" + type + "\" (float, bool or trigger)";
        return false;
      }
      param.initial = floatOf(spec, "default", 0.0f);
      const std::string source = textOf(spec, "source");
      if (source == "speed")
        param.source = Source::Speed;
      else if (source == "verticalSpeed")
        param.source = Source::VerticalSpeed;
      else if (source == "grounded")
        param.source = Source::Grounded;
      else if (!source.empty()) {
        error = "parameter \"" + name + "\" has source \"" + source + "\" (speed, verticalSpeed or grounded)";
        return false;
      }
      graph.params[name] = param;
    }
  }

  if (!data.contains("states") || !data["states"].is_array() || data["states"].empty()) {
    error = "\"states\" must be a list with at least one state";
    return false;
  }
  for (const json &item : data["states"]) {
    State state;
    state.name = textOf(item, "name");
    if (state.name.empty()) {
      error = "a state has no \"name\"";
      return false;
    }
    if (graph.findState(state.name) >= 0) {
      error = "two states are called \"" + state.name + "\"";
      return false;
    }
    state.speed = floatOf(item, "speed", 1.0f);
    if (item.contains("loop"))
      state.loop = floatOf(item, "loop", 1.0f) != 0.0f;
    if (item.contains("blend")) {
      const json &blend = item["blend"];
      state.blendParam = textOf(blend, "param");
      auto param = graph.params.find(state.blendParam);
      if (param == graph.params.end() || param->second.type != ParamType::Float) {
        error = "state \"" + state.name + "\": the blend needs a float parameter, \"" + state.blendParam +
                "\" is not one";
        return false;
      }
      if (!blend.contains("clips") || !blend["clips"].is_array() || blend["clips"].empty()) {
        error = "state \"" + state.name + "\": the blend needs a list of \"clips\"";
        return false;
      }
      for (const json &point : blend["clips"]) {
        BlendPoint blendPoint;
        blendPoint.clip = textOf(point, "clip");
        blendPoint.at = floatOf(point, "at", 0.0f);
        if (blendPoint.clip.empty()) {
          error = "state \"" + state.name + "\": a blend clip has no \"clip\"";
          return false;
        }
        state.blend.push_back(blendPoint);
      }
      std::stable_sort(state.blend.begin(), state.blend.end(),
                       [](const BlendPoint &a, const BlendPoint &b) { return a.at < b.at; });
    } else if (item.contains("clip") && item["clip"].is_string()) {
      state.clip = item["clip"].get<std::string>();
    } else {
      error = "state \"" + state.name + "\" has neither a \"clip\" nor a \"blend\"";
      return false;
    }
    graph.states.push_back(std::move(state));
  }

  if (data.contains("default")) {
    graph.defaultState = graph.findState(textOf(data, "default"));
    if (graph.defaultState < 0) {
      error = "\"default\" names no state";
      return false;
    }
  }

  if (data.contains("transitions")) {
    if (!data["transitions"].is_array()) {
      error = "\"transitions\" must be a list";
      return false;
    }
    for (const json &item : data["transitions"]) {
      Transition transition;
      const std::string from = textOf(item, "from", "any");
      const std::string to = textOf(item, "to");
      if (from != "any" && (transition.from = graph.findState(from)) < 0) {
        error = "a transition leaves \"" + from + "\", which is no state";
        return false;
      }
      if ((transition.to = graph.findState(to)) < 0) {
        error = "a transition goes to \"" + to + "\", which is no state";
        return false;
      }
      transition.fade = std::max(0.0f, floatOf(item, "fade", 0.2f));
      if (item.contains("exitTime"))
        transition.exitTime = std::max(0.0f, floatOf(item, "exitTime", 0.0f));
      if (item.contains("when")) {
        if (!item["when"].is_array()) {
          error = "\"when\" must be a list of conditions";
          return false;
        }
        for (const json &spec : item["when"]) {
          Condition condition;
          condition.param = textOf(spec, "param");
          auto param = graph.params.find(condition.param);
          if (param == graph.params.end()) {
            error = "a condition uses \"" + condition.param + "\", which is no parameter";
            return false;
          }
          const std::string op = textOf(spec, "op", param->second.type == ParamType::Trigger ? "trigger" : "true");
          if (!parseOp(op, condition.op)) {
            error = "a condition on \"" + condition.param + "\" has the operator \"" + op + "\"";
            return false;
          }
          const bool hasValue = spec.is_object() && spec.contains("value");
          condition.value = floatOf(spec, "value", 0.0f);
          // `== true` and `== false` on a bool read as the plain true and false conditions
          if (condition.op == Op::Equal && param->second.type != ParamType::Float && hasValue)
            condition.op = condition.value != 0.0f ? Op::IsTrue : Op::IsFalse;
          transition.when.push_back(condition);
        }
      }
      graph.transitions.push_back(std::move(transition));
    }
  }
  return true;
}
