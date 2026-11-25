-- Records each animation event of the entity's state machine in the shared `game` table, with what
-- self:inAnimationWindow(name) said at that moment
game.events = game.events or {}

function onAnimationEvent(self, name, phase, layer)
  game.events[#game.events + 1] = name .. ":" .. phase .. "@" .. layer .. "=" .. tostring(self:inAnimationWindow(name))
end
