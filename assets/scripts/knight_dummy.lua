-- The knight's training dummy: another knight (same model and animation state machine) that stands still facing the
-- player and takes hits. knight.lua calls its `takeHit` field when the sword's damage window touches it: it
-- flinches (the machine's additive `hit` layer), and after `hitsToDie` hits it falls (`dead`) and gets up again.
--
-- Like the player, the entity with the CharacterController is the capsule and the model hangs under it.

properties = {
  hitsToDie = 3,  -- hits it takes before it falls
  getUpAfter = 4, -- seconds it lies there
}

-- Called by the script that hit it. A dummy that is down takes no hits.
local function takeHit(self)
  if self.down then
    return false
  end
  self.hits = self.hits + 1
  self.model:trigger("hit")
  if self.hits >= self.hitsToDie then
    self.down = true
    self.model:setParam("dead", true)
    self:run(function()
      wait(self.getUpAfter)
      self.model:setParam("dead", false)
      self.hits = 0
      self.down = false
    end)
  end
  return true
end

function onStart(self)
  self.model = self:children()[1]
  self.hits = 0
  self.down = false
  self.takeHit = takeHit -- the way another script reaches it: other:takeHit()
end
