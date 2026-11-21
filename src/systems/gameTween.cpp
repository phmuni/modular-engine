// The game API for tweens: see gameTween.h.

#include "systems/gameTween.h"

#include "components/light.h"
#include "components/model.h"
#include "foundation/core/engine.h"
#include "rendering/resources/material.h"
#include "systems/resourceSystem.h"
#include <algorithm>
#include <cmath>

namespace {
// Robert Penner's easing functions, as on easings.net
float bounceOut(float t) {
  const float n1 = 7.5625f, d1 = 2.75f;
  if (t < 1.0f / d1)
    return n1 * t * t;
  if (t < 2.0f / d1) {
    t -= 1.5f / d1;
    return n1 * t * t + 0.75f;
  }
  if (t < 2.5f / d1) {
    t -= 2.25f / d1;
    return n1 * t * t + 0.9375f;
  }
  t -= 2.625f / d1;
  return n1 * t * t + 0.984375f;
}
} // namespace

float applyEase(Ease ease, float t) {
  t = std::min(std::max(t, 0.0f), 1.0f);
  if (t == 0.0f || t == 1.0f)
    return t; // the ends are exact whatever the curve (the elastic and back ones are not, in float arithmetic)
  constexpr float pi = 3.14159265358979f;
  const float c1 = 1.70158f, c2 = c1 * 1.525f, c3 = c1 + 1.0f;
  const float c4 = 2.0f * pi / 3.0f, c5 = 2.0f * pi / 4.5f;
  switch (ease) {
  case Ease::Linear:
    return t;
  case Ease::EaseInQuad:
    return t * t;
  case Ease::EaseOutQuad:
    return 1.0f - (1.0f - t) * (1.0f - t);
  case Ease::EaseInOutQuad:
    return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
  case Ease::EaseInCubic:
    return t * t * t;
  case Ease::EaseOutCubic:
    return 1.0f - std::pow(1.0f - t, 3.0f);
  case Ease::EaseInOutCubic:
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
  case Ease::EaseInSine:
    return 1.0f - std::cos(t * pi / 2.0f);
  case Ease::EaseOutSine:
    return std::sin(t * pi / 2.0f);
  case Ease::EaseInOutSine:
    return -(std::cos(pi * t) - 1.0f) / 2.0f;
  case Ease::EaseInBack:
    return c3 * t * t * t - c1 * t * t;
  case Ease::EaseOutBack:
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
  case Ease::EaseInOutBack:
    return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
                    : (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
  case Ease::EaseInElastic:
    return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
  case Ease::EaseOutElastic:
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
  case Ease::EaseInOutElastic:
    return t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
                    : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
  case Ease::EaseInBounce:
    return 1.0f - bounceOut(1.0f - t);
  case Ease::EaseOutBounce:
    return bounceOut(t);
  case Ease::EaseInOutBounce:
    return t < 0.5f ? (1.0f - bounceOut(1.0f - 2.0f * t)) / 2.0f : (1.0f + bounceOut(2.0f * t - 1.0f)) / 2.0f;
  }
  return t;
}

int GameTween::to(const GameEntity &entity, TweenProperty property, const glm::vec3 &target, float seconds, Ease ease,
                  std::function<void()> onComplete) {
  if (!entity.valid())
    return 0;
  glm::vec3 from(0.0f);
  float strength = 1.0f; // the emission's, kept while its color changes
  switch (property) {
  case TweenProperty::Position: from = entity.position(); break;
  case TweenProperty::Rotation: from = entity.rotation(); break;
  case TweenProperty::Scale: from = entity.scale(); break;
  case TweenProperty::Color:
    if (!entity.has("Model"))
      return 0;
    from = entity.color();
    break;
  case TweenProperty::Emission: {
    const Model *model = m_engine.tryGet<Model>(entity.id());
    if (!model || model->materialHandles.empty())
      return 0;
    const Material &material = m_engine.getSystemManager().getSystem<ResourceSystem>().getMaterial(model->materialHandles[0]);
    from = material.getEmissionColor();
    strength = material.getEmissionStrength() > 0.0f ? material.getEmissionStrength() : 1.0f;
    break;
  }
  case TweenProperty::Intensity:
    if (!entity.light())
      return 0;
    from = glm::vec3(entity.light()->intensity);
    break;
  }
  // A new tween of the same property takes over from the one running
  std::vector<int> replaced;
  for (const std::shared_ptr<Item> &item : m_items)
    if (item->owner == entity.id() && item->property == static_cast<int>(property))
      replaced.push_back(item->id);
  for (int id : replaced)
    remove(id);

  GameEntity handle = entity;
  auto apply = [handle, property, from, target, strength, last = glm::vec3(-1.0f)](float progress) mutable {
    const glm::vec3 now = glm::mix(from, target, progress);
    switch (property) {
    case TweenProperty::Position: handle.setPosition(now); break;
    case TweenProperty::Rotation: handle.setRotation(now); break;
    case TweenProperty::Scale: handle.setScale(now); break;
    case TweenProperty::Color: {
      // Colors are 8 bits per channel: a smaller step would only make a new solid-color texture for nothing
      const glm::vec3 step = glm::abs(now - last);
      if (progress >= 1.0f || step.x >= 1.0f / 255.0f || step.y >= 1.0f / 255.0f || step.z >= 1.0f / 255.0f) {
        handle.setColor(now);
        last = now;
      }
      break;
    }
    case TweenProperty::Emission: handle.setEmission(now, strength); break;
    case TweenProperty::Intensity:
      if (Light *light = handle.light())
        light->intensity = now.x;
      break;
    }
  };
  m_items.push_back(std::make_shared<Item>(Item{m_nextId, 0.0f, std::max(seconds, 0.0f), ease, entity.id(),
                                                 static_cast<int>(property), std::move(apply), std::move(onComplete),
                                                 false}));
  return m_nextId++;
}

int GameTween::value(float from, float to, float seconds, Ease ease, std::function<void(float)> onValue,
                     std::function<void()> onComplete) {
  auto apply = [from, to, onValue = std::move(onValue)](float progress) {
    if (onValue)
      onValue(from + (to - from) * progress);
  };
  m_items.push_back(std::make_shared<Item>(Item{m_nextId, 0.0f, std::max(seconds, 0.0f), ease, -1, -1,
                                                 std::move(apply), std::move(onComplete), false}));
  return m_nextId++;
}

void GameTween::remove(int id) {
  for (auto it = m_items.begin(); it != m_items.end(); ++it)
    if ((*it)->id == id) {
      (*it)->cancelled = true;
      m_items.erase(it);
      return;
    }
}

void GameTween::cancel(int tween) { remove(tween); }

void GameTween::clear() {
  for (const std::shared_ptr<Item> &item : m_items)
    item->cancelled = true;
  m_items.clear();
}

void GameTween::tick(float deltaTime) {
  if (m_items.empty())
    return;
  std::vector<int> running;
  running.reserve(m_items.size());
  for (const std::shared_ptr<Item> &item : m_items) {
    item->elapsed += deltaTime;
    running.push_back(item->id);
  }
  // By handle, since a callback may start or cancel tweens (any of these included)
  for (int id : running) {
    auto it = std::find_if(m_items.begin(), m_items.end(), [&](const std::shared_ptr<Item> &item) { return item->id == id; });
    if (it == m_items.end())
      continue;
    const std::shared_ptr<Item> item = *it; // kept alive while its callback runs
    if (item->owner != -1 && !m_engine.isAlive(item->owner)) {
      remove(id);
      continue;
    }
    const float progress = item->duration <= 0.0f ? 1.0f : std::min(item->elapsed / item->duration, 1.0f);
    item->apply(applyEase(item->ease, progress));
    if (progress < 1.0f || item->cancelled)
      continue;
    remove(id);
    if (item->onComplete)
      item->onComplete();
  }
}
