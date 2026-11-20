---@meta
-- The Lua API modular-engine gives scripts (autocomplete and type hints for the Lua extension, sumneko.lua).
-- Written from src/systems/scriptSystem.cpp; tools/check_lua_api.py fails when a binding is missing here.

-- Script callbacks ---------------------------------------------------------------------------------------------------
-- A script is a .lua file: an entity's (its Script component) or the game script (project.json). Define these as
-- global functions; every one is optional. An error in a callback is shown on screen and stops that script until the
-- file is saved again.

---Entity script: once before its first onUpdate.
---@alias OnStart fun(self: Entity)

---Entity script: every frame, with the seconds since the last one.
---@alias OnUpdate fun(self: Entity, dt: number)

---Entity script: when a physics body of this entity touches another one. `point` and `normal` are the contact's (the normal
---is flipped for the other entity's call). Called on both entities' scripts.
---@alias OnContact fun(self: Entity, other: Entity, point: vec3, normal: vec3)

---Entity script: every frame two collision shapes overlap (called on both entities' scripts).
---@alias OnOverlap fun(self: Entity, other: Entity)

---Entity script: after its file was saved and loaded again while the game runs (only if the script had started).
---@alias OnReload fun(self: Entity)

---The game script (assets/scripts/game.lua) has no `self`: `function onStart()`, `function onUpdate(dt)`, and its
---onReload gets nil.
---@alias GameOnUpdate fun(dt: number)

---The numbers, booleans, strings and vec3s the Properties panel lets you edit per entity. The values here are the
---defaults; the entity's own values are `self.<name>`. Other kinds of value are ignored.
---@type table<string, number|boolean|string|vec3>
properties = {}

---@type fun(self: Entity)
onStart = nil
---@type fun(self: Entity, dt: number)
onUpdate = nil
---@type fun(self: Entity, other: Entity, point: vec3, normal: vec3)
onContact = nil
---@type fun(self: Entity, other: Entity)
onOverlap = nil
---@type fun(self: Entity)
onReload = nil

-- vec3 ---------------------------------------------------------------------------------------------------------------

---A 3D vector (positions, rotations in degrees, colors 0-1, velocities).
---@class vec3
---@field x number
---@field y number
---@field z number
---@operator add(vec3): vec3
---@operator sub(vec3): vec3
---@operator mul(vec3): vec3
---@operator mul(number): vec3
---@operator div(number): vec3
---@operator unm: vec3
---@overload fun(): vec3
---@overload fun(v: number): vec3
---@overload fun(x: number, y: number, z: number): vec3
vec3 = {}

---Its length.
---@param self vec3
---@return number
function vec3.length(self) end

---The same direction with length 1 (the vector itself when its length is 0).
---@param self vec3
---@return vec3
function vec3.normalized(self) end

---The dot product.
---@param a vec3
---@param b vec3
---@return number
function vec3.dot(a, b) end

---The cross product.
---@param a vec3
---@param b vec3
---@return vec3
function vec3.cross(a, b) end

---The distance between two points.
---@param a vec3
---@param b vec3
---@return number
function vec3.distance(a, b) end

---The point between a (t = 0) and b (t = 1).
---@param a vec3
---@param b vec3
---@param t number
---@return vec3
function vec3.lerp(a, b, t) end

-- Components ---------------------------------------------------------------------------------------------------------

---An entity's position, rotation (degrees) and scale.
---@class Transform
---@field position vec3
---@field rotation vec3
---@field scale vec3

---A light component.
---@class Light
---@field color vec3
---@field intensity number

-- Entity (`self`, `other`, and what engine.find and friends return) --------------------------------------------------

---An entity. Any other key is the entity's own Lua field (`self.speed = 5`, `other.health`), kept when the script
---reloads; the script's `properties` are set there too.
---@class Entity
---@field [string] any
---@field id integer (read-only)
---@field valid boolean (read-only) false once the entity was destroyed
---@field name string
---@field transform Transform|nil (read-only)
---@field position vec3 Without a Transform, setting it does nothing
---@field rotation vec3 In degrees
---@field scale vec3 Size factor per axis
---@field forward vec3 (read-only) -Z turned by the entity's yaw (rotation.y), on the ground (y is 0)
---@field right vec3 (read-only) The way to its right, on the ground
---@field light Light|nil (read-only)
---@field script Entity|nil (read-only) the entity itself when it has a Script component, else nil
---@field visible boolean (read-only) false after setVisible(false)
---@field velocity vec3 Linear velocity of its physics body
---@field onGround boolean (read-only) whether its character controller stands on the ground
local Entity = {}

---Destroys the entity.
function Entity:destroy() end

---Whether it has an engine component: "Model", "RigidBody", "CharacterController", "Light", "Camera", "Animator",
---"Script", "Transform", "AudioSource", "Skybox", "Collision", "ParticleEmitter" or "Hidden", or a game component
---registered in C++ (by the name it was registered with).
---@param component string
---@return boolean
function Entity:has(component) end

---Its parent, or nil.
---@return Entity|nil
function Entity:parent() end

---Makes `parent` its parent (nil for none); false when the engine refuses.
---@param parent Entity|nil
---@return boolean
function Entity:setParent(parent) end

---Its children.
---@return Entity[]
function Entity:children() end

---Paints its model one solid color (0-1 per channel).
---@param color vec3
function Entity:setColor(color) end

---Makes its model glow with a color.
---@param color vec3
---@param strength? number Defaults to 1
function Entity:setEmission(color, strength) end

---Shows or hides it (and its model's shadow).
---@param visible boolean
function Entity:setVisible(visible) end

---Puts an image on its model (a part of the material).
---@param slot "diffuse"|"specular"|"normal"|"emission"
---@param path string Relative to assets/ ("textures/brick.png")
function Entity:setTexture(slot, path) end

---How shiny its model is (the specular exponent).
---@param shininess number
function Entity:setShininess(shininess) end

---Adds an impulse to its physics body.
---@param impulse vec3
function Entity:impulse(impulse) end

---Adds a force to its physics body.
---@param force vec3
function Entity:force(force) end

---Sets the velocity its character controller walks with.
---@param velocity vec3
function Entity:move(velocity) end

---Makes its character controller jump with this speed.
---@param speed number
function Entity:jump(speed) end

---Plays an animation clip.
---@param clip string
---@param fade? number Seconds to blend from the current clip. Defaults to 0.2
---@param loop? boolean Defaults to true
function Entity:play(clip, fade, loop) end

---The names of its animation clips.
---@return string[]
function Entity:clips() end

---Plays a sound from the entity's position.
---@param path string Relative to assets/ ("sounds/laser.wav")
---@param volume? number Defaults to 1
---@return integer handle For sound.stop, sound.setVolume and sound.isPlaying (0: nothing played)
function Entity:sound(path, volume) end

-- engine -------------------------------------------------------------------------------------------------------------

---Options for engine.create. Everything is optional.
---@class EngineCreateOptions
---@field model? string "box", "sphere", "cylinder" or a model path relative to assets/
---@field position? vec3
---@field rotation? vec3 Degrees
---@field scale? vec3
---@field color? vec3 Solid color
---@field emission? vec3 Glow color
---@field glow? number Emission strength. Defaults to 1.5
---@field body? "static"|"dynamic"|"kinematic" Gives it a physics body (any other string makes it dynamic)
---@field shape? "box"|"sphere"|"capsule"|"convexHull"|"mesh" The body's shape. Defaults to "box"
---@field mass? number
---@field collision? boolean Adds collision from its model
---@field script? string Script path relative to assets/ ("scripts/coin.lua")

---Entities, scenes and the window.
---@class engine
engine = {}

---The entity with this name, or nil.
---@param name string
---@return Entity|nil
function engine.find(name) end

---Every entity with this name, by entity number.
---@param name string
---@return Entity[]
function engine.findAll(name) end

---Every entity with a component, by entity number ("Model", "Script", or a game component registered in C++).
---@param component string
---@return Entity[]
function engine.findWithComponent(component) end

---Puts a copy of a prefab in the scene (its root at `position`, turned `rotation`).
---@param prefab string Path relative to assets/
---@param position? vec3
---@param rotation? vec3 Degrees
---@return Entity|nil
function engine.spawn(prefab, position, rotation) end

---Creates an entity.
---@param name string
---@param options? EngineCreateOptions
---@return Entity|nil
function engine.create(name, options) end

---Destroys an entity (nothing for nil or one already destroyed).
---@param entity Entity|nil
function engine.destroy(entity) end

---Loads a scene file, replacing the current one.
---@param path string Relative to assets/
function engine.loadScene(path) end

---Sets the window's title.
---@param title string
function engine.setTitle(title) end

---Closes the game at the end of this frame.
function engine.quit() end

---Sets the color behind everything (the skybox panorama, if there is one, draws over it). It is kept in the scene file
---(on the first Skybox component, which this adds when there is none) and shows in the editor.
---@param color vec3 0-1 per channel; black until set
function engine.setSkyColor(color) end

---The sky color.
---@return vec3
function engine.getSkyColor() end

-- game ---------------------------------------------------------------------------------------------------------------

---State shared by every script; it starts empty again when the game stops and plays again.
---@type table<string, any>
game = {}

-- camera -------------------------------------------------------------------------------------------------------------

---The active camera.
---@class camera
camera = {}

---Points the camera from a position at a target (does nothing while the free camera in Settings is on). A lookAt
---made in a frame wins over camera.follow for that frame.
---@param from vec3
---@param target vec3
function camera.lookAt(from, target) end

---Options for camera.follow. Everything is optional.
---@class CameraFollowOptions
---@field offset? vec3 Where the camera sits from the target: x to its right, y up, z behind it. Defaults to vec3(0, 4, 7)
---@field lookAhead? number How far in front of the target (along its facing) the camera looks. Defaults to 0
---@field smoothing? number Seconds the camera takes to catch up, for position and rotation together. 0 (the default) is rigid
---@field positionSmoothing? number Same, for the position only (wins over smoothing)
---@field rotationSmoothing? number Same, for the target's facing and the point looked at (wins over smoothing)
---@field followRotation? boolean The offset turns with the target, staying behind it. Defaults to true; false keeps the offset in world axes
---@field orbit? boolean Turns around the target with the mouse (while the cursor is captured, or the right button is held) and the gamepad's right stick
---@field orbitSensitivity? number Degrees per pixel of mouse movement. Defaults to 0.15
---@field orbitSpeed? number Degrees per second at full stick. Defaults to 120

---Makes the active camera trail an entity, from behind, after physics and scripts have moved it (so it does not
---jitter). Replaces the camera's follow settings. It stands aside while the free camera in Settings is on, and
---camera.lookAt in the same frame wins. A scene load replaces the camera: call it again in onSceneLoaded.
---Also a CameraRig component in the scene file, edited in the Properties panel.
---@param target Entity
---@param options? CameraFollowOptions
function camera.follow(target, options) end

---Stops following (removes the camera's rig); the camera stays where it is.
function camera.stopFollow() end

---Whether the active camera has a follow rig.
---@return boolean
function camera.following() end

---Where the camera is.
---@return vec3
function camera.position() end

---The direction the camera looks at.
---@return vec3
function camera.forward() end

---The vertical field of view in degrees: reads it, or sets it when given one (between 1 and 179).
---@param degrees? number
---@return number
function camera.fov(degrees) end

-- time ---------------------------------------------------------------------------------------------------------------

---Game time (what onUpdate's dt measures: it stands still while paused) and timers.
---@class time
time = {}

---The speed of time: 1 is normal, 0.5 slow motion, 0 stands still. Sets it when given a number.
---@param scale? number
---@return number
function time.scale(scale) end

---Whether the game is paused (like F7 and the pause menu). Pauses or resumes when given a boolean.
---@param paused? boolean
---@return boolean
function time.paused(paused) end

---Seconds of game time since the game started.
---@return number
function time.elapsed() end

---Seconds of game time this frame took (onUpdate's dt).
---@return number
function time.delta() end

---Calls a function once after some seconds of game time.
---@param seconds number
---@param action fun()
---@return integer handle For time.cancel
function time.after(seconds, action) end

---Calls a function every so many seconds of game time.
---@param seconds number
---@param action fun()
---@return integer handle For time.cancel
function time.every(seconds, action) end

---Stops a timer (nothing for one that already ran).
---@param handle integer
function time.cancel(handle) end

-- input --------------------------------------------------------------------------------------------------------------

---Actions and axes (assets/input.json) and raw keys, named as SDL names them ("Space", "Left Shift").
---@class input
input = {}

---The strongest value of an axis's bindings.
---@param name string
---@return number
function input.axis(name) end

---Whether an action is held down.
---@param name string
---@return boolean
function input.down(name) end

---Whether an action went down this frame.
---@param name string
---@return boolean
function input.pressed(name) end

---Whether an action was let go this frame.
---@param name string
---@return boolean
function input.released(name) end

---Whether a key is held down (false for a name SDL does not know).
---@param name string
---@return boolean
function input.key(name) end

---Whether a key went down this frame.
---@param name string
---@return boolean
function input.keyPressed(name) end

---The cursor's position in window pixels (top-left origin).
---@return number x
---@return number y
function input.mouse() end

---Whether a mouse button went down this frame.
---@param button? integer 1 left, 2 middle, 3 right. Defaults to 1
---@return boolean
function input.click(button) end

---Whether a mouse button is held down.
---@param button? integer 1 left, 2 middle, 3 right. Defaults to 1
---@return boolean
function input.mouseDown(button) end

---How far the wheel turned this frame (positive: away from you).
---@return number
function input.wheel() end

---Loads actions and axes from a JSON file; false if it cannot be read.
---@param path string Relative to assets/
---@return boolean
function input.bindings(path) end

-- physics ------------------------------------------------------------------------------------------------------------

---What physics.raycast found.
---@class RaycastHit
---@field entity Entity|nil
---@field point vec3
---@field normal vec3
---@field distance number

---Physics bodies.
---@class physics
physics = {}

---Casts a ray and returns the first thing it hits, or nil.
---@param origin vec3
---@param direction vec3
---@param distance? number Defaults to 1000
---@return RaycastHit|nil
function physics.raycast(origin, direction, distance) end

---Sets the gravity when given one; returns the current gravity.
---@param gravity? vec3
---@return vec3
function physics.gravity(gravity) end

-- nav ----------------------------------------------------------------------------------------------------------------

---Options for nav.build. Everything is optional; without min and max the map covers the static bodies.
---@class NavBuildOptions
---@field min? vec3
---@field max? vec3
---@field cellSize? number
---@field agentRadius? number
---@field agentHeight? number
---@field maxSlope? number Degrees
---@field stepHeight? number

---Path finding over a grid of walkable cells.
---@class nav
nav = {}

---Builds the navigation map; returns whether it worked.
---@param options? NavBuildOptions
---@return boolean
function nav.build(options) end

---The points to walk, on the ground, or nil when there is no way (or no map).
---@param from vec3
---@param to vec3
---@return vec3[]|nil
function nav.path(from, to) end

---Whether a point is on walkable ground.
---@param point vec3
---@return boolean
function nav.walkable(point) end

-- sound --------------------------------------------------------------------------------------------------------------

---Sound effects and music. Paths are relative to assets/.
---@class sound
sound = {}

---Plays a sound.
---@param path string
---@param volume? number Defaults to 1
---@param pitch? number Defaults to 1
---@return integer handle For sound.stop, sound.setVolume and sound.isPlaying (0: nothing played)
function sound.play(path, volume, pitch) end

---Plays a sound from a position in the world.
---@param path string
---@param position vec3
---@param volume? number Defaults to 1
---@return integer handle As sound.play returns
function sound.playAt(path, position, volume) end

---Stops a sound that is playing (nothing for one that ended).
---@param handle integer
function sound.stop(handle) end

---Changes the volume of a sound that is playing.
---@param handle integer
---@param volume number
function sound.setVolume(handle, volume) end

---Whether a sound is still playing.
---@param handle integer
---@return boolean
function sound.isPlaying(handle) end

---Reads a sound in the background, so its first play does not wait for the file.
---@param path string
function sound.preload(path) end

---Plays music.
---@param path string
---@param volume? number Defaults to 1
function sound.music(path, volume) end

---Stops the music.
function sound.stopMusic() end

---Changes the volume of the music playing now.
---@param volume number
function sound.musicVolume(volume) end

-- save ---------------------------------------------------------------------------------------------------------------

---Progress and settings that stay between runs. Values are numbers, strings, booleans or tables of those, nested.
---@class save
save = {}

---Sets a value (anything else is reported as an error and not saved).
---@param key string
---@param value number|string|boolean|table
function save.set(key, value) end

---A saved value, or `fallback` when there is none.
---@param key string
---@param fallback? any
---@return any
function save.get(key, fallback) end

---Whether a key has a value.
---@param key string
---@return boolean
function save.has(key) end

---Forgets a key.
---@param key string
function save.remove(key) end

---Writes the saved data to disk; returns whether it worked.
---@return boolean
function save.write() end

-- ui -----------------------------------------------------------------------------------------------------------------

---RmlUi documents (.rml), changed by element id.
---@class ui
ui = {}

---Loads and shows a document (the same one when loaded again); false if it cannot be loaded.
---@param path string Relative to assets/
---@return boolean
function ui.load(path) end

---Enables the ready-made pause menu (Resume, Options, Quit): Escape opens it.
---@param enabled? boolean Defaults to true
function ui.pauseMenu(enabled) end

---Opens the pause menu from code (works without ui.pauseMenu).
function ui.openPauseMenu() end

---Closes the pause menu.
function ui.closePauseMenu() end

---Whether the pause menu is open.
---@return boolean
function ui.isPaused() end

---Sets an element's content (the value goes through tostring).
---@param id string
---@param value any
function ui.text(id, value) end

---Shows or hides an element.
---@param id string
---@param visible boolean
function ui.show(id, visible) end

---Adds or removes a CSS class of an element.
---@param id string
---@param name string
---@param on boolean
function ui.class(id, name, on) end

---Sets one style property of an element ("color", "#ff0000"; "width", "50%"); "" goes back to the stylesheet's.
---@param id string
---@param property string
---@param value string
function ui.style(id, property, value) end

---Sets an attribute of an element ("src" of an image, "value" of an input).
---@param id string
---@param name string
---@param value string
function ui.attribute(id, name, value) end

---Calls a function when an element is clicked; false if there is no such element.
---@param id string
---@param handler fun()
---@return boolean
function ui.onClick(id, handler) end
