-- Records each state change of the entity's animation state machine in the shared `game` table
game.changes = game.changes or {}

function onStateChanged(self, old, new)
  game.changes[#game.changes + 1] = old .. ">" .. new
end
