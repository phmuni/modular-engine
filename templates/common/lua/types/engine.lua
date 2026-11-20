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
---@field layer string Its collision layer (see physics.addLayer), for its body, character and collision box; "default" without any. Takes effect at once, also on a body that is already simulating; a name that is no layer is a warning and changes nothing
---@field audioPlaying boolean (read-only) its AudioSource is playing (not paused; false once a sound that does not loop ended)
---@field audioVolume number Its AudioSource's volume, 1 is normal (0 without an AudioSource)
---@field audioPitch number Its AudioSource's pitch, 1 is as recorded
---@field audioLoop boolean Whether its AudioSource starts over when it ends
local Entity = {}

---Destroys the entity.
function Entity:destroy() end

---Gives it a tag (a label like "enemy" to find groups of entities by: engine.findTagged). An entity has each tag once.
---Tags are saved in the scene and prefab files, and edited in the Properties panel.
---@param tag string
function Entity:addTag(tag) end

---Takes a tag away.
---@param tag string
function Entity:removeTag(tag) end

---Whether it has the tag.
---@param tag string
---@return boolean
function Entity:hasTag(tag) end

---Its tags.
---@return string[]
function Entity:tags() end

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
---@param part? integer|string Only this part of the model: a submesh number (from 0, as the editor lists them) or the name of an OBJ object/group or glTF node; every part without it
function Entity:setColor(color, part) end

---The color of its model's first part (white without a model).
---@return vec3
function Entity:getColor() end

---Runs a function as a coroutine that can `wait`, and ends it when the entity is destroyed:
---`self:run(function() wait(1) self:setColor(vec3(1, 0, 0)) wait(0.5) self:destroy() end)`.
---It starts at once, up to its first wait. Waits follow game time (time scale and pause). An error inside is reported
---on screen like any script error, and ends the coroutine.
---@param fn fun()
---@return integer handle For engine.stop
function Entity:run(fn) end

---Makes its model glow with a color.
---@param color vec3
---@param strength? number Defaults to 1
---@param part? integer|string Only this part of the model: a submesh number (from 0, as the editor lists them) or the name of an OBJ object/group or glTF node; every part without it
function Entity:setEmission(color, strength, part) end

---Shows or hides it (and its model's shadow).
---@param visible boolean
function Entity:setVisible(visible) end

---Puts an image on its model (a part of the material).
---@param slot "diffuse"|"specular"|"normal"|"emission"
---@param path string Relative to assets/ ("textures/brick.png")
---@param part? integer|string Only this part of the model: a submesh number (from 0, as the editor lists them) or the name of an OBJ object/group or glTF node; every part without it
function Entity:setTexture(slot, path, part) end

---How shiny its model is (the specular exponent).
---@param shininess number
---@param part? integer|string Only this part of the model: a submesh number (from 0, as the editor lists them) or the name of an OBJ object/group or glTF node; every part without it
function Entity:setShininess(shininess, part) end

---Sizes its Collision box (made if it has none) to its model, as rotated and scaled now. Call it again after changing the
---rotation or scale a lot: the box itself does not rotate.
---@param fit? number Scales the box around its center: below 1 for forgiving hitboxes, above 1 for generous ones. Defaults to 1
---@param isStatic? boolean A box that never moves counts only against moving ones. Defaults to false
function Entity:fitCollision(fit, isStatic) end

---Whether its Collision box overlaps the other's now (the boxes `collision = true` in engine.create makes, or
---fitCollision). Positions are as they are when called, so move, then ask. For physics bodies there is onContact.
---@param other Entity|nil
---@return boolean
function Entity:isTouching(other) end

---Draws a part (or all of them) of its model with a custom surface shader: a .glsl file with a
---`void surface(inout Surface s)` and/or `void vertex(inout VertexData v)` function that the engine adds to its lit
---shader, so lights, shadows and fog still apply (see "Custom surface shaders" in the README; examples in
---shaders/examples/). Only this entity changes, not others made from the same model. A file that does not compile is
---logged with its name and the model keeps the standard shader.
---@param path? string Relative to assets/ ("shaders/examples/water.glsl"); nil or "" is back to the standard shader
---@param submesh? integer The model's part, counting from 0 as the editor lists them; nil is every part
function Entity:setShader(path, submesh) end

---The shader file a part uses ("" for the standard shader).
---@param submesh? integer The part, counting from 0 (the first by default)
---@return string
function Entity:shader(submesh) end

---Gives one of its custom shader's uniforms a value (set the shader first). A name the shader does not declare, or a
---value of the wrong kind, is a warning.
---@param name string The uniform's name in the .glsl file
---@param value number|vec3|number[]|string A number for a float; a vec3 or a table of 2, 3 or 4 numbers for a vecN; a
--- texture path relative to assets/ for a sampler2D
---@param submesh? integer The part, counting from 0; nil is every part
function Entity:setShaderParam(name, value, submesh) end

---A custom shader uniform's value on a part: what was set, or the file's default. Floats come back as numbers, vec3 as a
---vec3, vec2 and vec4 as tables of numbers, textures as their path; nil for a name the shader does not have.
---@param name string
---@param submesh? integer The part, counting from 0 (the first by default)
---@return number|vec3|number[]|string|nil
function Entity:shaderParam(name, submesh) end

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

---Starts its AudioSource (the sound attached to the entity, added in the editor; it follows the entity as it moves)
---over from the start, or continues it after pauseAudio. Does nothing without an AudioSource.
function Entity:playAudio() end

---Stops its AudioSource and rewinds it.
function Entity:stopAudio() end

---Holds its AudioSource where it is; playAudio continues from there.
function Entity:pauseAudio() end

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
---@field tags? string[] Tags to give it, e.g. {"enemy"}
---@field layer? string Collision layer of its body, character and collision box: "default", "static", "player", "enemy", "projectile", "trigger", or one from physics.addLayer. Defaults to "default"
---@field character? CharacterOptions|boolean A walking character (true for the defaults); it is its own physics body, so it replaces `body`. Its position is the capsule's middle
---@field components? string[] Game components registered by C++ (Engine::registerComponent), by name, added with their default values: {"Player"}. A project with no C++ has none

---Options for a character in engine.create. Everything is optional.
---@class CharacterOptions
---@field radius? number Defaults to 0.35
---@field height? number Total, round ends included. Defaults to 1.8
---@field maxSlope? number Steepest slope it climbs, in degrees. Defaults to 50
---@field stepHeight? number Highest step it climbs. Defaults to 0.35

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

---Runs a function as a coroutine that can `wait`; it lasts across scene loads (use self:run for one that should end
---with an entity). It starts at once, up to its first wait. Waits are game time, so they follow time.scale and stand
---still while paused. An error inside is reported on screen like any script error, and ends the coroutine.
---```lua
---engine.run(function()
---  ui.text("message", "Get ready")
---  wait(2)
---  ui.text("message", "Go!")
---end)
---```
---@param fn fun()
---@return integer handle For engine.stop
function engine.run(fn) end

---Stops a coroutine started with engine.run or self:run (nothing for one that already ended).
---@param handle integer
function engine.stop(handle) end

---Suspends the coroutine it is called in for that many seconds of game time; wait() until the next frame. Only inside
---engine.run / self:run (anywhere in the functions they call): elsewhere it is an error.
---@param seconds? number
function wait(seconds) end

---Every entity with this tag (self:addTag("enemy"), or tags = {"enemy"} in engine.create), by entity number.
---@param tag string
---@return Entity[]
function engine.findTagged(tag) end

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

---The scene file last loaded (or saved), "" before one was.
---@return string
function engine.scenePath() end

---Whether a loadScene is still waiting for its models (the old scene keeps running until they are ready).
---@return boolean
function engine.isLoadingScene() end

---Reads a model on a worker thread, so creating it later (engine.create, engine.spawn) does not stall a frame.
---@param path string Relative to assets/
function engine.preload(path) end

---Saves a BMP of the next frame.
---@param path string
---@param delay? number Seconds to wait after start-up first. Defaults to 0
---@param quitAfter? boolean Closes the game afterwards. Defaults to false
function engine.screenshot(path, delay, quitAfter) end

---The ray from the active camera through a point of the window, for picking. Takes window coordinates, so
---`engine.screenRay(input.mouse())` works as it is.
---@param x number
---@param y number
---@return vec3|nil origin
---@return vec3|nil direction Unit length; nil with both when there is no active camera
function engine.screenRay(x, y) end

---The nearest visible model under a point of the window (triangle accurate, unlike physics bodies):
---`local hit = engine.pick(input.mouse())`.
---@param x number
---@param y number
---@return RaycastHit|nil
function engine.pick(x, y) end

---Sets the window's title.
---@param title string
function engine.setTitle(title) end

---Resizes the window, in window units (points on a Retina screen, pixels elsewhere; not framebuffer pixels). Does
---nothing for a size that is not positive. In fullscreen it is the size the window takes when it leaves it.
---@param width integer
---@param height integer
function engine.setWindowSize(width, height) end

---The window's size in window units, after the system applied the last setWindowSize.
---@return integer width
---@return integer height
function engine.windowSize() end

---Window sizes that fit the display the window is on (usable area, without taskbar or menu bar), smallest first: the
---common 16:9 sizes and the display's own. Never empty.
---@return { width: integer, height: integer }[]
function engine.resolutions() end

---The 3D scene is drawn at the framebuffer's pixels times this scale and stretched onto the window with linear
---filtering: below 1 is faster, above 1 sharper (supersampling). Clamped to 0.5 to 2; 1 by default. The editor and the
---game UI keep drawing at the framebuffer's own size.
---@param scale number
function engine.setRenderScale(scale) end

---The render scale (0.5 to 2).
---@return number
function engine.renderScale() end

---The scene's size in pixels now: the framebuffer's size (the window's, times its pixel density) times the render
---scale.
---@return integer width
---@return integer height
function engine.renderResolution() end

---Borderless fullscreen at the desktop's size; the window comes back at its old size when it is turned off.
---@param enabled boolean
function engine.setFullscreen(enabled) end

---Whether the window is fullscreen.
---@return boolean
function engine.isFullscreen() end

---@alias engine.Toggle
---| "freeCamera"          # the player flies the camera (WASD + mouse) instead of the game; off by default
---| "freeCameraVertical"  # the free camera also moves up and down (Space / Shift); on by default
---| "cursorLock"          # the mouse is held by the window, for looking around
---| "wireframe"
---| "showUI"              # the editor (F2)
---| "shadows"
---| "showStats"           # the performance overlay (F5)
---| "vsync"
---| "paused"              # the simulation stops; the camera and UI keep going
---| "gammaCorrect"
---| "uiDebugger"          # the game UI debugger (F9)
---| "bloom"

---Turns one of the engine's switches on or off, the same ones the F keys and the editor's Settings change. An unknown
---name is an error that lists the known ones.
---@param name engine.Toggle
---@param on boolean
function engine.setToggle(name, on) end

---Whether one of the engine's switches is on.
---@param name engine.Toggle
---@return boolean
function engine.isToggled(name) end

---Closes the game at the end of this frame.
function engine.quit() end

---Sets the color behind everything (the skybox panorama, if there is one, draws over it). It is kept in the scene file
---(on the first Skybox component, which this adds when there is none) and shows in the editor.
---@param color vec3 0-1 per channel; black until set
function engine.setSkyColor(color) end

---The sky color.
---@return vec3
function engine.getSkyColor() end

---Settings for engine.setFog. Everything is optional.
---@class FogOptions
---@field enabled? boolean Defaults to true when a table is given
---@field color? vec3 The fog color; without one the fog takes the sky color (engine.setSkyColor), so distant things fade into the sky
---@field matchSky? boolean Take the sky color (the default until a `color` is given)
---@field mode? "linear"|"exponential" "linear" is clear up to `start` and solid from `finish`; "exponential" thickens smoothly with `density`
---@field start? number Linear: distance from the camera where it begins. Defaults to 10
---@field finish? number Linear: distance where it is solid. Defaults to 100
---@field density? number Exponential: per world unit, about 0.02 is gentle. Defaults to 0.02

---Fog for the scene, kept in the scene file with the sky and shown in the editor's Skybox panel. Lit things blend toward
---the fog color with distance; particles fade out. Only the fields given change; `false` turns it off.
---@param settings FogOptions|false
function engine.setFog(settings) end

---The fog's settings. `color` is the sky color while `matchSky` is true.
---@return FogOptions
function engine.getFog() end

---Light added to everything lit, from nowhere, on top of each Light's own `ambient`: color times intensity (an intensity of 0 adds
---nothing, which is how scenes start). Kept in the scene file with the sky.
---@param color vec3 0-1 per channel
---@param intensity? number Defaults to 1
function engine.setAmbient(color, intensity) end

---The scene's ambient light.
---@return vec3 color
---@return number intensity
function engine.getAmbient() end

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
---camera.lookAt in the same frame wins. It survives engine.loadScene: the request (target name and options) is put on the
---new scene's camera, found by name, unless that camera has a CameraRig of its own in the scene file, which wins (the
---request stays for later scenes). camera.stopFollow forgets it.
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

-- tween --------------------------------------------------------------------------------------------------------------

---Easing curves for tweens: how the speed changes from start to end.
---@alias Easing "linear"|"easeInQuad"|"easeOutQuad"|"easeInOutQuad"|"easeInCubic"|"easeOutCubic"|"easeInOutCubic"

---Animates an entity's properties or a number over time, on game time (a paused game holds them). The end value is
---reached exactly.
---@class tween
tween = {}

---Moves a property of the entity from what it is now to a value: "position", "rotation" (degrees) or "scale" take a
---vec3 (a number for all three axes), "color" a vec3 (the entity needs a model). A tween of the same property that is
---running is replaced; one whose entity is destroyed stops without calling onComplete. Tweening the position of a
---dynamic body fights the physics.
---```lua
---tween.to(self, "position", vec3(0, 5, 0), 0.5, "easeOutQuad", function() self:destroy() end)
---```
---@param entity Entity
---@param property "position"|"rotation"|"scale"|"color"
---@param value vec3|number
---@param seconds number
---@param easing? Easing Defaults to "linear"
---@param onComplete? fun()
---@return integer handle For tween.cancel (0 if nothing started)
function tween.to(entity, property, value, seconds, easing, onComplete) end

---Calls a function every frame with a number going from `from` to `to` (ending exactly on `to`).
---@param from number
---@param to number
---@param seconds number
---@param easing? Easing Defaults to "linear"
---@param onValue fun(value: number)
---@param onComplete? fun()
---@return integer handle For tween.cancel
function tween.value(from, to, seconds, easing, onValue, onComplete) end

---Stops a tween where it is, without calling onComplete (nothing for one that already finished).
---@param handle integer
function tween.cancel(handle) end

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

---How far the mouse moved this frame, in pixels (zero while a menu has the input): for mouse-look, with the mouse
---captured so it never reaches the window's edge.
---@return number dx To the right
---@return number dy Down
function input.mouseDelta() end

---Captures the mouse (hidden, held in the window, delta keeps moving) for mouse-look, or frees it for clicking menus.
---The same switch as Settings' "Cursor lock" (Right Alt), so the editor and the player can still flip it; the pause menu
---frees it while open and puts back what you asked for after. The engine starts with it captured. While captured, the
---game UI and the editor get no mouse.
---@param captured boolean
function input.captureMouse(captured) end

---Whether the mouse is captured.
---@return boolean
function input.isMouseCaptured() end

---Shows or hides the cursor while the mouse is free (a game that draws its own). It is shown anyway while the pause
---menu is open and the editor panels (F2) are.
---@param visible boolean
function input.showCursor(visible) end

---Whether the cursor is meant to be shown.
---@return boolean
function input.isCursorVisible() end

---Starts text entry, e.g. a name: typed characters (and Backspace, UTF-8 aware) go to input.text() instead of the game.
---While it is on, keys and actions read as idle (W does not walk while a name is typed), except Backspace, Delete,
---Enter, Escape, Tab, the arrows, Home, End and the F keys, which input.keyPressed still sees (Enter to finish).
---@param initial? string The text to start from. Defaults to ""
---@param maxLength? integer Most characters allowed. Defaults to no limit
function input.startText(initial, maxLength) end

---Ends text entry; input.text() keeps what was typed until the next start.
function input.stopText() end

---Whether text entry is on.
---@return boolean
function input.isTextActive() end

---The text typed so far, with Backspace applied.
---@return string
function input.text() end

---Replaces the text typed so far.
---@param text string
function input.setText(text) end

---Only what was typed this frame, for a buffer of your own.
---@return string
function input.typed() end

---Rumbles the connected gamepads: `low` is the big motor (a deep rumble), `high` the small one (a buzz), 0 to 1 each, for
---some seconds of real time (a pause does not hold it). A new call replaces the one running.
---@param low number
---@param high number
---@param seconds? number Defaults to 0.2
---@return boolean taken Whether a gamepad that can rumble took it (false with none connected)
function input.rumble(low, high, seconds) end

---Stops the rumble.
function input.stopRumble() end

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

---Options for physics.raycast.
---@class RaycastOptions
---@field layers? string[] Only bodies on these collision layers count, e.g. {"enemy", "static"}; a name that is no layer is an error. Every layer without it

---Casts a ray and returns the first thing it hits, or nil.
---@param origin vec3
---@param direction vec3
---@param distance? number Defaults to 1000
---@param options? RaycastOptions
---@return RaycastHit|nil
function physics.raycast(origin, direction, distance, options) end

---Like physics.raycast, for Collision boxes (`collision = true` in engine.create) instead of physics bodies.
---@param origin vec3
---@param direction vec3
---@param distance? number Defaults to 1000
---@param options? RaycastOptions
---@return RaycastHit|nil
function physics.raycastBoxes(origin, direction, distance, options) end

---Casts a ray against the triangles of visible models, and returns the nearest hit with the surface normal there, or nil.
---@param origin vec3
---@param direction vec3
---@param distance? number Unlimited without it
---@return RaycastHit|nil
function physics.raycastModels(origin, direction, distance) end

---Defines a collision layer (up to 32 in all; "default", "static", "player", "enemy", "projectile" and "trigger" always exist). Layers
---are for entity.layer, `layer =` in engine.create and the editor's dropdowns; a Lua project can also list them in project.json:
---`"layers": { "names": ["water"], "ignore": [["enemy", "enemy"]] }`.
---@param name string
function physics.addLayer(name) end

---The names of the collision layers.
---@return string[]
function physics.layers() end

---Says whether things on two layers collide. Everything collides until told otherwise, e.g. physics.setLayersCollide("enemy", "enemy", false)
---makes enemies pass through each other, and ("projectile", "player", false) lets bullets through the player. It applies to physics bodies,
---characters and Collision boxes, and takes effect at once. A name that is no layer is an error.
---@param a string
---@param b string
---@param collide boolean
function physics.setLayersCollide(a, b, collide) end

---Whether things on two layers collide.
---@param a string
---@param b string
---@return boolean
function physics.layersCollide(a, b) end

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

---Plays music, replacing what plays.
---@param path string
---@param volume? number Defaults to 1
---@param loop? boolean Defaults to true
---@return integer handle For the music calls below, so a handle of replaced music leaves the newer alone (0: nothing played)
function sound.music(path, volume, loop) end

---Stops the music (the music of the handle, if given and still the one playing).
---@param handle? integer
function sound.stopMusic(handle) end

---Changes the volume of the music playing now (or of the music of the handle, if it still plays).
---@param volume number
---@param handle? integer
function sound.musicVolume(volume, handle) end

---Whether music plays: false once music that does not loop has ended, or for a handle that was replaced.
---@param handle? integer
---@return boolean
function sound.isMusicPlaying(handle) end

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

---Loads a font file; documents use it by the family name inside the file (Lato is there already).
---@param path string Relative to assets/
---@param fallback? boolean Also supplies glyphs the other fonts lack (symbols, other scripts). Defaults to false
---@return boolean ok False if the file could not be read
function ui.loadFont(path, fallback) end

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
