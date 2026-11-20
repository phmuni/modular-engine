-- Custom surface shaders from Lua: entity:setShader / shader / setShaderParam / shaderParam, on one entity without
-- touching another made from the same model, parameters of every kind, and a file that does not compile

local DISSOLVE = "shaders/examples/dissolve.glsl"

test.run(function()
  local a = engine.create("A", { model = "box", position = vec3(0, 0, 0) })
  local b = engine.create("B", { model = "box", position = vec3(3, 0, 0) })
  test.eq(a:shader(), "", "no shader at first")

  -- Only the entity it was set on
  a:setShader(DISSOLVE)
  test.eq(a:shader(), DISSOLVE)
  test.eq(a:shader(0), DISSOLVE, "the part, counting from 0")
  test.eq(b:shader(), "", "another entity of the same model keeps the standard shader")
  wait()
  wait()

  -- Parameters: the file's defaults, then what was set, of each kind
  test.near(a:shaderParam("dissolve"), 0.4)
  test.near(a:shaderParam("edgeColor"), vec3(1, 0.45, 0.1))
  a:setShaderParam("dissolve", 0.75)
  test.near(a:shaderParam("dissolve"), 0.75)
  a:setShaderParam("edgeColor", vec3(0, 1, 0))
  test.near(a:shaderParam("edgeColor"), vec3(0, 1, 0))
  a:setShaderParam("edgeColor", { 0.5, 0.25, 0.125 }) -- a table of three numbers is a vec3 too
  test.near(a:shaderParam("edgeColor"), vec3(0.5, 0.25, 0.125))
  a:setShaderParam("noiseScale", 2, 0)
  test.near(a:shaderParam("noiseScale", 0), 2)
  test.eq(a:shaderParam("nothing"), nil, "a name the shader does not have")
  a:setShaderParam("nothing", 1) -- a warning, nothing set
  test.eq(a:shaderParam("nothing"), nil)
  test.eq(b:shaderParam("dissolve"), nil, "no shader on b, no parameters")

  -- Vectors of 2 and 4 are tables; textures are paths (a shader file written for the test)
  local file = (os.tmpname() .. ".glsl"):gsub("\\", "/") -- the engine reports "/" paths
  local out = io.open(file, "w")
  out:write("uniform vec2 scroll = vec2(1.0, 2.0);\nuniform vec4 extra;\nuniform sampler2D maskTexture;\n")
  out:write("void surface(inout Surface s) { s.albedo *= extra.rgb + texture(maskTexture, s.uv + scroll).rgb; }\n")
  out:close()
  b:setShader(file)
  test.eq(b:shader(), file)
  test.eq(a:shaderParam("scroll"), nil)
  test.eq(b:shaderParam("scroll"), { 1, 2 })
  b:setShaderParam("scroll", { 3, 4 })
  test.eq(b:shaderParam("scroll"), { 3, 4 })
  b:setShaderParam("extra", { 1, 2, 3, 4 })
  test.eq(b:shaderParam("extra"), { 1, 2, 3, 4 })
  b:setShaderParam("maskTexture", "models/box/diffuse.png")
  test.eq(b:shaderParam("maskTexture"), "models/box/diffuse.png")
  wait()
  os.remove(file)

  -- A value of no kind a uniform has is an error the script can catch
  local ok = pcall(function() b:setShaderParam("scroll", true) end)
  test.ok(not ok, "a boolean is not a shader value")
  ok = pcall(function() b:setShaderParam("scroll", { 1 }) end)
  test.ok(not ok, "one number in a table is not a vector")

  -- A file that does not compile: the entity keeps drawing, remembers the file, and the game goes on
  local broken = (os.tmpname() .. ".glsl"):gsub("\\", "/")
  out = io.open(broken, "w")
  out:write("void surface(inout Surface s) { s.albedo = vec3(; }\n")
  out:close()
  local c = engine.create("C", { model = "box" })
  c:setShader(broken)
  test.eq(c:shader(), broken)
  wait()
  wait()
  test.ok(c.valid, "still running")
  os.remove(broken)

  -- Back to the standard shader: nil, "" or for one part
  a:setShader(nil)
  test.eq(a:shader(), "")
  test.eq(a:shaderParam("dissolve"), nil)
  b:setShader("")
  test.eq(b:shader(), "")
  c:setShader(DISSOLVE, 0)
  test.eq(c:shader(), DISSOLVE)
  c:setShader(nil, 0)
  test.eq(c:shader(), "")
  wait()
end)
