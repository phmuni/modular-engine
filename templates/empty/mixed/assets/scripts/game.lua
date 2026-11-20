-- The game script: runs for the whole game, once the scene has loaded (src/main.cpp starts it). Its `game` table is
-- shared with all of the entities' scripts. Every function here is optional.

-- Once, when it starts.
function onStart()
  -- ui.pauseMenu(true) -- uncomment for an Escape menu: assets/ui/pauseMenu.rml (Resume, Options, Quit)
end

-- Every frame; dt is the seconds of game time it took (it stands still while the game is paused).
function onUpdate(dt)
end

-- A pause menu button with data-event-click="action('name')" calls this with its name.
-- function onPauseMenuAction(name)
-- end
