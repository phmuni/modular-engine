-- The game script: runs for the whole game, before the entities' scripts. Its `game` table is shared with all of them.
-- Every function here is optional.

-- Once, when the game starts and the scene (assets/scenes/level.json) has loaded.
function onStart()
  -- ui.pauseMenu(true) -- uncomment for an Escape menu: assets/ui/pauseMenu.rml (Resume, Options, Quit)
end

-- Every frame; dt is the seconds of game time it took (it stands still while the game is paused).
function onUpdate(dt)
end

-- A pause menu button with data-event-click="action('name')" calls this with the menu ("pause") and its name.
-- function onMenuAction(menu, name)
-- end
