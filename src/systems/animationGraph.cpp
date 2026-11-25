// Reading animation state machine definitions.

#include "systems/animationGraph.h"

#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int AnimationGraph::StateSet::findState(const std::string &name) const {
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

// Where an error belongs, for the messages: "" for the base, ` of layer "upper"` for a layer
std::string where(const std::string &layer) { return layer.empty() ? std::string() : " of layer \"" + layer + "\""; }

bool parseStates(const json &list, const std::map<std::string, AnimationGraph::Param> &params,
                 AnimationGraph::StateSet &set, bool allowEmpty, const std::string &layer, std::string &error) {
  for (const json &item : list) {
    AnimationGraph::State state;
    state.name = textOf(item, "name");
    if (state.name.empty()) {
      error = "a state" + where(layer) + " has no \"name\"";
      return false;
    }
    if (set.findState(state.name) >= 0) {
      error = "two states" + where(layer) + " are called \"" + state.name + "\"";
      return false;
    }
    state.speed = floatOf(item, "speed", 1.0f);
    if (item.contains("loop"))
      state.loop = floatOf(item, "loop", 1.0f) != 0.0f;
    state.reference = textOf(item, "reference");
    if (item.contains("blend")) {
      const json &blend = item["blend"];
      state.blendParam = textOf(blend, "param");
      auto param = params.find(state.blendParam);
      if (param == params.end() || param->second.type != AnimationGraph::ParamType::Float) {
        error = "state \"" + state.name + "\": the blend needs a float parameter, \"" + state.blendParam +
                "\" is not one";
        return false;
      }
      if (!blend.contains("clips") || !blend["clips"].is_array() || blend["clips"].empty()) {
        error = "state \"" + state.name + "\": the blend needs a list of \"clips\"";
        return false;
      }
      for (const json &point : blend["clips"]) {
        AnimationGraph::BlendPoint blendPoint;
        blendPoint.clip = textOf(point, "clip");
        blendPoint.at = floatOf(point, "at", 0.0f);
        if (blendPoint.clip.empty()) {
          error = "state \"" + state.name + "\": a blend clip has no \"clip\"";
          return false;
        }
        state.blend.push_back(blendPoint);
      }
      std::stable_sort(state.blend.begin(), state.blend.end(),
                       [](const AnimationGraph::BlendPoint &a, const AnimationGraph::BlendPoint &b) {
                         return a.at < b.at;
                       });
    } else if (item.contains("clip") && item["clip"].is_string()) {
      state.clip = item["clip"].get<std::string>();
    } else if (allowEmpty) {
      state.empty = true; // a layer's state that plays nothing
    } else {
      error = "state \"" + state.name +
              "\" has neither a \"clip\" nor a \"blend\" (only a layer's state may play nothing)";
      return false;
    }
    set.states.push_back(std::move(state));
  }
  return true;
}

bool parseTransitions(const json &list, const std::map<std::string, AnimationGraph::Param> &params,
                      AnimationGraph::StateSet &set, const std::string &layer, std::string &error) {
  using Op = AnimationGraph::Op;
  if (!list.is_array()) {
    error = "\"transitions\"" + where(layer) + " must be a list";
    return false;
  }
  for (const json &item : list) {
    AnimationGraph::Transition transition;
    const std::string from = textOf(item, "from", "any");
    const std::string to = textOf(item, "to");
    if (from != "any" && (transition.from = set.findState(from)) < 0) {
      error = "a transition leaves \"" + from + "\", which is no state" + where(layer);
      return false;
    }
    if ((transition.to = set.findState(to)) < 0) {
      error = "a transition goes to \"" + to + "\", which is no state" + where(layer);
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
        AnimationGraph::Condition condition;
        condition.param = textOf(spec, "param");
        auto param = params.find(condition.param);
        if (param == params.end()) {
          error = "a condition uses \"" + condition.param + "\", which is no parameter";
          return false;
        }
        const std::string op =
            textOf(spec, "op", param->second.type == AnimationGraph::ParamType::Trigger ? "trigger" : "true");
        if (!parseOp(op, condition.op)) {
          error = "a condition on \"" + condition.param + "\" has the operator \"" + op + "\"";
          return false;
        }
        const bool hasValue = spec.is_object() && spec.contains("value");
        condition.value = floatOf(spec, "value", 0.0f);
        // `== true` and `== false` on a bool read as the plain true and false conditions
        if (condition.op == Op::Equal && param->second.type != AnimationGraph::ParamType::Float && hasValue)
          condition.op = condition.value != 0.0f ? Op::IsTrue : Op::IsFalse;
        transition.when.push_back(condition);
      }
    }
    set.transitions.push_back(std::move(transition));
  }
  return true;
}

bool parseLayer(const json &item, const AnimationGraph &graph, AnimationGraph::Layer &layer, std::string &error) {
  layer.name = textOf(item, "name");
  if (layer.name.empty()) {
    error = "a layer has no \"name\"";
    return false;
  }
  if (layer.name == AnimationGraph::kBaseLayer) {
    error = "a layer cannot be called \"base\": that is the top-level states";
    return false;
  }
  for (const AnimationGraph::Layer &other : graph.layers)
    if (other.name == layer.name) {
      error = "two layers are called \"" + layer.name + "\"";
      return false;
    }
  layer.additive = item.contains("additive") && floatOf(item, "additive", 0.0f) != 0.0f;

  if (item.contains("weight")) {
    const json &weight = item["weight"];
    if (weight.is_string()) {
      layer.weightParam = weight.get<std::string>();
      auto param = graph.params.find(layer.weightParam);
      if (param == graph.params.end() || param->second.type != AnimationGraph::ParamType::Float) {
        error = "layer \"" + layer.name + "\": the weight needs a float parameter, \"" + layer.weightParam +
                "\" is not one";
        return false;
      }
    } else if (weight.is_number()) {
      layer.weight = std::clamp(weight.get<float>(), 0.0f, 1.0f);
    } else {
      error = "layer \"" + layer.name + "\": \"weight\" is a number or the name of a float parameter";
      return false;
    }
  }

  if (item.contains("mask")) {
    const json &mask = item["mask"];
    layer.masked = true;
    if (mask.is_array()) {
      for (const json &node : mask) {
        if (!node.is_string()) {
          error = "layer \"" + layer.name + "\": the mask lists node names";
          return false;
        }
        layer.mask.emplace_back(node.get<std::string>(), 1.0f);
      }
    } else if (mask.is_object()) {
      for (const auto &[node, weight] : mask.items()) {
        if (!weight.is_number()) {
          error = "layer \"" + layer.name + "\": the mask weight of \"" + node + "\" is not a number";
          return false;
        }
        layer.mask.emplace_back(node, std::clamp(weight.get<float>(), 0.0f, 1.0f));
      }
    } else {
      error = "layer \"" + layer.name + "\": \"mask\" is a list of node names or an object of name: weight";
      return false;
    }
    if (layer.mask.empty()) {
      error = "layer \"" + layer.name + "\": the mask names no node (leave it out for the whole skeleton)";
      return false;
    }
  }

  if (!item.contains("states") || !item["states"].is_array() || item["states"].empty()) {
    error = "layer \"" + layer.name + "\": \"states\" must be a list with at least one state";
    return false;
  }
  if (!parseStates(item["states"], graph.params, layer, true, layer.name, error))
    return false;
  if (item.contains("default")) {
    layer.defaultState = layer.findState(textOf(item, "default"));
    if (layer.defaultState < 0) {
      error = "layer \"" + layer.name + "\": \"default\" names no state of it";
      return false;
    }
  }
  if (item.contains("transitions") && !parseTransitions(item["transitions"], graph.params, layer, layer.name, error))
    return false;
  return true;
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
  if (!parseStates(data["states"], graph.params, graph.base, false, std::string(), error))
    return false;

  if (data.contains("default")) {
    graph.base.defaultState = graph.base.findState(textOf(data, "default"));
    if (graph.base.defaultState < 0) {
      error = "\"default\" names no state";
      return false;
    }
  }

  if (data.contains("transitions") &&
      !parseTransitions(data["transitions"], graph.params, graph.base, std::string(), error))
    return false;

  if (data.contains("layers")) {
    if (!data["layers"].is_array()) {
      error = "\"layers\" must be a list";
      return false;
    }
    for (const json &item : data["layers"]) {
      Layer layer;
      if (!parseLayer(item, graph, layer, error))
        return false;
      graph.layers.push_back(std::move(layer));
    }
  }
  return true;
}
