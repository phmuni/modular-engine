#pragma once
// The definition of an animation state machine, read from a JSON file (see the README, "Animation state machines"):
// what the AnimationStateMachine component's `path` names. A graph is only data; the AnimationSystem runs it.

#include <map>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

struct AnimationGraph {
  enum class ParamType { Float, Bool, Trigger };
  // Where a parameter's value comes from on its own each frame, for an entity with a CharacterController
  enum class Source { None, Speed, VerticalSpeed, Grounded };
  enum class Op { Greater, Less, GreaterEqual, LessEqual, Equal, NotEqual, IsTrue, IsFalse, Triggered };

  struct Param {
    ParamType type = ParamType::Float;
    float initial = 0.0f;
    Source source = Source::None;
  };
  struct BlendPoint {
    std::string clip;
    float at = 0.0f; // the parameter value at which this clip plays alone
  };
  struct State {
    std::string name;
    std::string clip; // empty with a blend: the blend's clips play
    float speed = 1.0f;
    bool loop = true;
    std::string blendParam;
    std::vector<BlendPoint> blend; // ascending by `at`
  };
  struct Condition {
    std::string param;
    Op op = Op::IsTrue;
    float value = 0.0f;
  };
  struct Transition {
    int from = -1; // a state's index; -1: from any state
    int to = 0;
    float fade = 0.2f;      // seconds
    float exitTime = -1.0f; // the share of the state's clip played before it may fire; negative: no such wait
    std::vector<Condition> when; // all of them hold
  };

  std::map<std::string, Param> params;
  std::vector<State> states;
  std::vector<Transition> transitions;
  int defaultState = 0;

  int findState(const std::string &name) const;

  // Reads a graph; false with the reason in `error` for a file that is not one (unknown state or parameter, no states)
  static bool parse(const nlohmann::json &data, AnimationGraph &graph, std::string &error);
};
