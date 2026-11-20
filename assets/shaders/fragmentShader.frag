#version 330 core
out vec4 FragColor;

struct Material {
    sampler2D diffuse;
    sampler2D specular;
    sampler2D normal; // tangent-space normal map (BC5 or RGB); only X and Y are read
    sampler2D emission;
    float shininess;
    vec3 emissionColor;
    float emissionStrength;
}; 

struct Light {
    vec3 position;
    vec3 direction;
    vec3 color; 
    float intensity;
    float ambient;
    float constant;
    float linear;
    float quadratic;
    float cutOff;
    float outerCutOff;
    int type;
    int shadowSlot;  // point shadow cube map, -1 for none
    float shadowFar; // its far plane (Light::shadowRange)
};

in vec3 FragPos;  
in vec3 Normal;  
in vec2 TexCoords;
in vec4 Tangent;

uniform vec3 viewPos;
uniform Material material;
uniform int numLights;
uniform Light lights[10];

// Sun shadow: cascades fitted to the view (EngineConfig::SHADOW_CASCADES layers), compared and filtered in hardware
uniform sampler2DArrayShadow shadowMap;
uniform mat4 cascadeMatrices[4];
uniform float cascadeSplits[4]; // where each cascade ends, as distance along viewForward
uniform float cascadeTexels[4]; // world size of one texel in each cascade
uniform int cascadeCount;
uniform vec3 viewForward;

// Point light shadows, one cube map per slot (EngineConfig::MAX_POINT_SHADOWS). Separate samplers rather than an
// array: GLSL 3.30 only indexes sampler arrays with constants.
uniform samplerCubeShadow pointShadowMaps0;
uniform samplerCubeShadow pointShadowMaps1;
uniform samplerCubeShadow pointShadowMaps2;
uniform samplerCubeShadow pointShadowMaps3;
uniform float pointShadowTexel; // one texel of a cube face at distance 1 (2 / POINT_SHADOW_SIZE)
uniform int useShadows;
uniform float opacity;
uniform int useNormalMap;
uniform int useSolidDiffuseColor;
uniform vec3 solidDiffuseColor;

// glTF-style material inputs (see AlphaMode)
uniform vec4 baseColorFactor; // tints the diffuse texture; alpha multiplies its alpha
uniform int alphaMode;        // 0 opaque, 1 mask, 2 blend
uniform float alphaCutoff;
uniform int doubleSided;
uniform int unlit; // KHR_materials_unlit: base color only, no lights or shadows

// Colors (textures, solid colors, light colors) are authored in sRGB. With gammaCorrect, lighting is computed on
// linear values, which keeps midtones and light falloff from looking too dark. The output is HDR: the composite pass
// (composite.frag) tone maps it and converts it for display.
uniform int gammaCorrect;

// The scene's environment (the Skybox component), already in the space the scene target is in: light added to
// everything lit, and fog that blends toward the sky's color with distance. fogMode 0 skips the fog.
uniform vec3 sceneAmbient;
uniform int fogMode; // 0 off, 1 linear, 2 exponential
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogFinish;
uniform float fogDensity;

vec3 toLinear(vec3 c) { return gammaCorrect == 1 ? pow(c, vec3(2.2)) : c; }

float calculateShadow(vec3 normal)
{
    if (useShadows == 0) return 0.0;

    // The cascade that covers this distance; beyond the last there is no sun shadow
    float depth = dot(FragPos - viewPos, viewForward);
    int cascade = -1;
    for (int i = 0; i < cascadeCount; ++i) {
        if (depth < cascadeSplits[i]) {
            cascade = i;
            break;
        }
    }
    if (cascade < 0) return 0.0;

    // Normal offset of a couple of texels against acne; texels grow from cascade to cascade, so the offset does too
    vec3 offsetPos = FragPos + normal * cascadeTexels[cascade] * 1.5;
    vec3 projCoords = (cascadeMatrices[cascade] * vec4(offsetPos, 1.0)).xyz * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 0.0;

    // 3x3 PCF: each tap is already a filtered 2x2 comparison. Taps outside the map read the border (lit).
    float lit = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize * 1.5;
            lit += texture(shadowMap, vec4(projCoords.xy + offset, float(cascade), projCoords.z - 0.0005));
        }
    }
    float shadow = 1.0 - lit / 9.0;

    // Fades out over the last tenth of the shadow distance instead of ending in a line
    float end = cascadeSplits[cascadeCount - 1];
    return shadow * (1.0 - smoothstep(end * 0.9, end, depth));
}

float samplePointShadow(int slot, vec4 coord)
{
    if (slot == 0) return texture(pointShadowMaps0, coord);
    if (slot == 1) return texture(pointShadowMaps1, coord);
    if (slot == 2) return texture(pointShadowMaps2, coord);
    return texture(pointShadowMaps3, coord);
}

// The depth the cube face facing `toFrag` stored for that point: a perspective depth along the major axis, with
// near 0.05 (RenderSystem's kPointShadowNear) and far = shadowFar
float cubeFaceDepth(vec3 toFrag, float far)
{
    const float near = 0.05;
    vec3 a = abs(toFrag);
    float z = max(a.x, max(a.y, a.z));
    float ndc = (far + near) / (far - near) - (2.0 * far * near) / ((far - near) * z);
    return ndc * 0.5 + 0.5;
}

float calculatePointShadow(Light light, vec3 normal)
{
    if (light.shadowSlot < 0) return 0.0;
    vec3 toFrag = FragPos - light.position;
    float dist = length(toFrag);
    if (dist >= light.shadowFar) return 0.0;

    // Normal offset of about two texels at this distance keeps lit surfaces from shadowing themselves (acne);
    // it grows with distance, as the texels do
    float texel = pointShadowTexel * dist;
    vec3 offsetPos = FragPos + normal * texel * 2.0;
    vec3 dir = offsetPos - light.position;

    // 5 taps across the face (each a filtered 2x2 comparison) soften the edge
    vec3 side = normalize(abs(dir.y) < 0.99 * length(dir) ? cross(dir, vec3(0.0, 1.0, 0.0)) : cross(dir, vec3(1.0, 0.0, 0.0)));
    vec3 up = normalize(cross(dir, side));
    vec3 taps[5] = vec3[](vec3(0.0), side, -side, up, -up);
    float lit = 0.0;
    for (int i = 0; i < 5; ++i) {
        vec3 tapDir = dir + taps[i] * texel * 1.5;
        lit += samplePointShadow(light.shadowSlot, vec4(tapDir, cubeFaceDepth(tapDir, light.shadowFar)));
    }
    return 1.0 - lit / 5.0;
}

void main()
{
    vec3 normal = normalize(Normal);
    // Back faces of double-sided materials are lit as the front
    if (doubleSided == 1 && !gl_FrontFacing)
        normal = -normal;
    // Shadow lookups offset along the surface itself: the normal map's bumps would make them miss
    vec3 surfaceNormal = normal;

    // Sampled unconditionally, like the other textures; without a normal map the result is not used
    vec2 normalTexel = texture(material.normal, TexCoords).rg * 2.0 - 1.0;
    if (useNormalMap == 1 && dot(Tangent.xyz, Tangent.xyz) > 0.0) {
        // Tangent frame: the tangent made perpendicular to the interpolated normal, the bitangent from their cross
        vec3 tangent = normalize(Tangent.xyz - normal * dot(normal, Tangent.xyz));
        vec3 bitangent = cross(normal, tangent) * Tangent.w;
        vec3 mapped = vec3(normalTexel, sqrt(max(1.0 - dot(normalTexel, normalTexel), 0.0)));
        normal = normalize(mat3(tangent, bitangent, normal) * mapped);
    }

    vec3 viewDir = normalize(viewPos - FragPos);

    // Sampled unconditionally: texture lookups in a branch get unreliable derivatives at triangle edges
    vec4 diffuseTexel = texture(material.diffuse, TexCoords);
    vec4 diffuseSample = useSolidDiffuseColor == 1 ? vec4(solidDiffuseColor, 1.0)
                                                   : diffuseTexel * vec4(baseColorFactor.rgb, 1.0);
    diffuseSample.a *= baseColorFactor.a;
#ifdef ALPHA_MASK
    // Only in the mask variant: any discard in a shader turns off hidden surface removal on tile-based GPUs,
    // which slows down every opaque draw that uses it
    if (diffuseSample.a < alphaCutoff)
        discard;
#endif
    vec3 diffuseTex  = toLinear(diffuseSample.rgb);
    vec3 specularTex = texture(material.specular, TexCoords).rgb;
    vec3 emissionTex = texture(material.emission, TexCoords).rgb;
    vec3 emissionResult = toLinear(material.emissionColor) * material.emissionStrength * toLinear(emissionTex);

    float shadow = calculateShadow(surfaceNormal);

    vec3 result = emissionResult;

    if (unlit == 1) {
        result += diffuseTex;
    } else if (numLights == 0) {
        result += diffuseTex * 0.7;
    } else {
        result += sceneAmbient * diffuseTex;
        for (int i = 0; i < numLights; ++i) {

            Light light = lights[i];
            light.color = toLinear(light.color);

            vec3 lightDir;
            if (light.type == 0)
                lightDir = normalize(-light.direction);
            else
                lightDir = normalize(light.position - FragPos);

            vec3 ambient = light.color * light.ambient * diffuseTex;

            float diff = max(dot(normal, lightDir), 0.0);
            vec3 diffuse = light.color * light.intensity * diff * diffuseTex;

            float shininess = max(material.shininess, 1.0);
            vec3 reflectDir = reflect(-lightDir, normal);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
            vec3 specular = light.color * light.intensity * spec * specularTex;

            float attenuation = 1.0;
            if (light.type == 1 || light.type == 2) {
                float dist = length(light.position - FragPos);
                attenuation = 1.0 /
                    (light.constant + light.linear * dist + light.quadratic * (dist * dist));
            }

            float spotFactor = 1.0;
            if (light.type == 2) {
                float theta = dot(lightDir, normalize(-light.direction));
                float epsilon = max(light.cutOff - light.outerCutOff, 1e-5);
                spotFactor = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
            }

            float shadowFactor = (light.type == 0) ? shadow : calculatePointShadow(light, surfaceNormal);

            vec3 lightResult =
                ambient +
                (1.0 - shadowFactor) * (diffuse + specular);

            lightResult *= attenuation * spotFactor;

            result += lightResult;
        }
    }

    if (fogMode != 0) {
        float dist = length(viewPos - FragPos);
        float fog = fogMode == 1 ? clamp((dist - fogStart) / (fogFinish - fogStart), 0.0, 1.0)
                                 : 1.0 - exp(-dist * dist * fogDensity * fogDensity);
        result = mix(result, fogColor, fog);
    }

    // Cut-out materials are opaque where they are not discarded
    float alpha = alphaMode == 1 ? 1.0 : diffuseSample.a;
    FragColor = vec4(max(result, vec3(0.0)), alpha * opacity);
}
