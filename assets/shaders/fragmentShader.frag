#version 410 core
out vec4 FragColor;

// PBR metallic-roughness (glTF): the base color is `diffuse`, times baseColorFactor below
struct Material {
    sampler2D diffuse;
    sampler2D metallicRoughness; // G = roughness, B = metallic; white (so the factors alone decide) when a material has none
    sampler2D normal; // tangent-space normal map (BC5 or RGB); only X and Y are read
    sampler2D emission;
    sampler2D occlusionMap; // R = ambient occlusion; white when a material has none
    float metallic;  // multiplies the map's B
    float roughness; // multiplies the map's G
    float occlusionStrength;
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

// Point light shadows: one cube map array, a cube per slot (EngineConfig::MAX_POINT_SHADOWS)
uniform samplerCubeArrayShadow pointShadowMaps;
uniform float pointShadowTexel; // one texel of a cube face at distance 1 (2 / POINT_SHADOW_SIZE)
// Image-based lighting (see ibl.h): the sky as diffuse light and reflections. iblMode 0 is none, 1 one color from every
// direction (iblColor), 2 the sky's image (its diffuse light as spherical harmonics, its reflections as the cube map
// below), times iblColor (the sky's intensity and the environment's).
uniform int iblMode;
uniform vec3 iblColor;
uniform float iblRotation; // radians around +Y: the panorama's rotation, applied to the lookup vectors
uniform float iblMaxLod;   // the pre-filtered maps' last mip level: roughness 1
uniform vec3 iblSh[9];     // the sky's diffuse light (sphericalHarmonics.h)
uniform samplerCube iblPrefiltered;
uniform sampler2D iblBrdf; // split-sum scale and bias by (NdotV, roughness)
// Reflection probes (reflectionProbes.h): up to 4 whose boxes overlap what is drawn, picked on the CPU per draw. Each
// brings its box, its diffuse light as spherical harmonics and a layer of the cube map array (reflections, pre-filtered
// like the sky's). Inside a box a probe replaces the sky, fading in over its blend distance from the box's edges.
uniform int probeCount;
uniform vec3 probeCenter[4];
uniform vec3 probeMin[4];
uniform vec3 probeMax[4];
uniform vec4 probeInfo[4]; // x blend distance, y intensity, z layer in probeCubes, w 1 when reflections follow the box
uniform vec3 probeSh[36];  // 9 per probe
uniform samplerCubeArray probeCubes;
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

// Ambient occlusion (see ssao.h): darkens the ambient light only, never a direct light or emission. Off (useAmbientOcclusion
// 0) for the transparent pass and when SSAO is off, which leaves the ambient exactly as it was.
uniform sampler2D aoTexture;
uniform int useAmbientOcclusion;
uniform vec2 aoTexel; // 1 / the render size
// The depth prepass (RenderSystem) runs this same program with only what decides coverage: the vertex shader's
// position, then the mask and custom discards, and nothing else
uniform int depthOnly;

vec3 toLinear(vec3 c) { return gammaCorrect == 1 ? pow(c, vec3(2.2)) : c; }

// Alpha-to-coverage (MSAA on): cut-out edges get a partial coverage instead of a hard discard. Set for the opaque
// draws of cut-out materials and of custom shaders that call cutBelow(); the alpha written below is then the coverage.
uniform int alphaToCoverage;
float cutCoverage = 1.0; // what cutBelow() leaves of the surface, for the alpha written at the end

#ifdef CUSTOM_SURFACE
// For custom shaders: cut the surface away where `value` is below `threshold`. A discard, or with MSAA a smooth
// edge (alpha-to-coverage, about one pixel wide).
void cutBelow(float value, float threshold) {
    float width = max(fwidth(value), 1e-5);
    if (alphaToCoverage == 1)
        cutCoverage = min(cutCoverage, clamp((value - threshold) / width + 0.5, 0.0, 1.0));
    else if (value < threshold)
        discard;
}

// A custom surface shader (see README, "Custom surface shaders"): its code goes in at the marker, and surface() is
// called once the base color and textures are known, before lighting. Colors are authored values, like the
// material's own: the engine converts them for lighting afterwards.
uniform float time; // seconds of game time
struct Surface {
    vec3 albedo;
    float alpha;
    vec3 emission;   // added to the material's own emission
    float metallic;  // 0 dielectric, 1 metal (the material's value and map)
    float roughness; // 0 mirror, 1 matte
    float occlusion; // 1 unoccluded; darkens the ambient light only
    vec3 normal;     // world space, after the normal map
    vec2 uv;
    vec3 worldPos;
    vec3 viewDir;    // from the surface to the camera
};
//@CUSTOM_SURFACE_CODE
#endif

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
        lit += texture(pointShadowMaps, vec4(tapDir, float(light.shadowSlot)), cubeFaceDepth(tapDir, light.shadowFar));
    }
    return 1.0 - lit / 5.0;
}

// ---- PBR (Cook-Torrance: GGX distribution, Smith-GGX visibility, Schlick Fresnel) ----
const float PI = 3.14159265359;
const float MIN_ROUGHNESS = 0.045; // a perfectly smooth surface would have a highlight smaller than a pixel

float distributionGGX(float NdotH, float a2)
{
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Height-correlated Smith-GGX, already divided by 4 NdotL NdotV
float visibilitySmithGGX(float NdotL, float NdotV, float a2)
{
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// The scale and bias of the split-sum environment BRDF, as an analytic fit (Karis, "Physically Based Shading on
// Mobile"): the specular reflectance of F0 under a uniform environment is F0 * x + y
vec2 environmentBrdf(float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// Ambient light of one color, on a surface: the old diffuse term (albedo * ambient, none for a metal) and that color
// reflected by the surface's F0, so metals are not black. The scene's `ambient` and each light's `ambient` go through
// here; the sky's lighting is environmentLight.
vec3 ambientLight(vec3 ambientColor, vec3 albedo, float metallic, float roughness, vec3 F0, float NdotV, float occlusion)
{
    vec3 diffuse = albedo * (1.0 - metallic) * ambientColor;
    vec2 brdf = environmentBrdf(roughness, NdotV);
    vec3 specular = ambientColor * (F0 * brdf.x + brdf.y);
    return (diffuse + specular) * occlusion;
}

// The polynomials of the spherical harmonics' basis (sphericalHarmonics.h) of a unit direction
void shBasis(vec3 n, out float b[9])
{
    b[0] = 1.0;
    b[1] = n.y;
    b[2] = n.z;
    b[3] = n.x;
    b[4] = n.x * n.y;
    b[5] = n.y * n.z;
    b[6] = 3.0 * n.z * n.z - 1.0;
    b[7] = n.x * n.z;
    b[8] = n.x * n.x - n.y * n.y;
}

// Image-based lighting: the diffuse light (irradiance as spherical harmonics) and the reflections (pre-filtered by
// roughness, split-sum with the BRDF table) of the sky and of the reflection probes around, blended: a probe's weight
// is how far inside its box the point is (0 at the edge, 1 from its blend distance in), and the sky fills what the
// weights leave of 1. Occlusion darkens both: the specular one with a cheaper, weaker term (Lagarde).
vec3 environmentLight(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0, float NdotV, float occlusion)
{
    if (iblMode == 0 && probeCount == 0)
        return vec3(0.0);
    vec2 brdf = textureLod(iblBrdf, vec2(NdotV, roughness), 0.0).rg;
    vec3 specularColor = F0 * brdf.x + brdf.y;
    // Rough surfaces reflect a direction nearer the normal than the mirror one
    vec3 R = reflect(-V, N);
    R = normalize(mix(N, R, (1.0 - roughness) * (sqrt(1.0 - roughness) + roughness)));
    float lod = roughness * iblMaxLod;

    float weights[4];
    float total = 0.0;
    for (int i = 0; i < probeCount; ++i) {
        vec3 inside = min(FragPos - probeMin[i], probeMax[i] - FragPos);
        float depth = min(inside.x, min(inside.y, inside.z));
        float blend = probeInfo[i].x;
        weights[i] = blend > 1e-4 ? clamp(depth / blend, 0.0, 1.0) : (depth >= 0.0 ? 1.0 : 0.0);
        total += weights[i];
    }
    float scale = total > 1.0 ? 1.0 / total : 1.0;
    float skyWeight = 1.0 - min(total, 1.0);

    vec3 irradiance = vec3(0.0);
    vec3 reflected = vec3(0.0);
    if (iblMode != 0 && skyWeight > 0.0) {
        vec3 skyIrradiance = iblColor;
        vec3 skyReflected = iblColor;
        if (iblMode == 2) {
            float c = cos(iblRotation), s = sin(iblRotation);
            vec3 rotatedN = vec3(c * N.x + s * N.z, N.y, -s * N.x + c * N.z);
            vec3 rotatedR = vec3(c * R.x + s * R.z, R.y, -s * R.x + c * R.z);
            float basis[9];
            shBasis(rotatedN, basis);
            vec3 sh = vec3(0.0);
            for (int k = 0; k < 9; ++k)
                sh += iblSh[k] * basis[k];
            skyIrradiance = max(sh, vec3(0.0)) * iblColor;
            skyReflected = textureLod(iblPrefiltered, rotatedR, lod).rgb * iblColor;
        }
        irradiance += skyIrradiance * skyWeight;
        reflected += skyReflected * skyWeight;
    }
    if (probeCount > 0) {
        float basis[9];
        shBasis(N, basis);
        for (int i = 0; i < probeCount; ++i) {
            float w = weights[i] * scale * probeInfo[i].y;
            if (w <= 0.0)
                continue;
            vec3 sh = vec3(0.0);
            for (int k = 0; k < 9; ++k)
                sh += probeSh[i * 9 + k] * basis[k];
            irradiance += max(sh, vec3(0.0)) * w;
            vec3 dir = R;
            if (probeInfo[i].w > 0.5) {
                // Where the reflection ray leaves the box, seen from the probe's center: a wall appears where it is
                vec3 safeR = mix(R, vec3(1e-5), lessThan(abs(R), vec3(1e-5)));
                vec3 toMax = (probeMax[i] - FragPos) / safeR;
                vec3 toMin = (probeMin[i] - FragPos) / safeR;
                vec3 far = max(toMax, toMin);
                float t = max(min(far.x, min(far.y, far.z)), 0.0);
                dir = FragPos + R * t - probeCenter[i];
            }
            reflected += textureLod(probeCubes, vec4(dir, probeInfo[i].z), lod).rgb * w;
        }
    }
    vec3 diffuse = irradiance * albedo * (1.0 - metallic) * (vec3(1.0) - specularColor);
    float specularOcclusion = clamp(pow(NdotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
    return diffuse * occlusion + reflected * specularColor * specularOcclusion;
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
    vec3 metallicRoughnessTexel = texture(material.metallicRoughness, TexCoords).rgb;
    float metallic = clamp(material.metallic * metallicRoughnessTexel.b, 0.0, 1.0);
    float roughness = clamp(material.roughness * metallicRoughnessTexel.g, 0.0, 1.0);
    float materialOcclusion = 1.0 + material.occlusionStrength * (texture(material.occlusionMap, TexCoords).r - 1.0);
    vec4 diffuseSample = useSolidDiffuseColor == 1 ? vec4(solidDiffuseColor, 1.0)
                                                   : diffuseTexel * vec4(baseColorFactor.rgb, 1.0);
    diffuseSample.a *= baseColorFactor.a;
#ifdef CUSTOM_SURFACE
    Surface surf;
    surf.albedo = diffuseSample.rgb;
    surf.alpha = diffuseSample.a;
    surf.emission = vec3(0.0);
    surf.metallic = metallic;
    surf.roughness = roughness;
    surf.occlusion = materialOcclusion;
    surf.normal = normal;
    surf.uv = TexCoords;
    surf.worldPos = FragPos;
    surf.viewDir = viewDir;
#ifdef CUSTOM_HAS_SURFACE
    surface(surf);
#endif
    diffuseSample = vec4(surf.albedo, surf.alpha);
    normal = normalize(surf.normal);
    metallic = clamp(surf.metallic, 0.0, 1.0);
    roughness = clamp(surf.roughness, 0.0, 1.0);
    materialOcclusion = clamp(surf.occlusion, 0.0, 1.0);
#endif
#ifdef ALPHA_MASK
    // Only in the mask variant: any discard in a shader turns off hidden surface removal on tile-based GPUs,
    // which slows down every opaque draw that uses it
    if (alphaToCoverage == 1)
        cutCoverage = min(cutCoverage, clamp((diffuseSample.a - alphaCutoff) / max(fwidth(diffuseSample.a), 1e-4) + 0.5, 0.0, 1.0));
    else if (diffuseSample.a < alphaCutoff)
        discard;
#endif
    if (depthOnly == 1) {
        FragColor = vec4(0.0);
        return;
    }
    // SSAO and the material's occlusion map darken the ambient light only
    float occlusion = (useAmbientOcclusion == 1 ? texture(aoTexture, gl_FragCoord.xy * aoTexel).r : 1.0) * materialOcclusion;
    vec3 albedo = toLinear(diffuseSample.rgb);
    vec3 emissionTex = texture(material.emission, TexCoords).rgb;
    vec3 emissionResult = toLinear(material.emissionColor) * material.emissionStrength * toLinear(emissionTex);
#ifdef CUSTOM_SURFACE
    emissionResult += toLinear(surf.emission);
#endif

    float shadow = calculateShadow(surfaceNormal);

    vec3 result = emissionResult;

    // F0: what a dielectric reflects head-on (4%) and a metal reflects of its base color
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    roughness = max(roughness, MIN_ROUGHNESS);
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotV = max(dot(normal, viewDir), 1e-4);

    if (unlit == 1) {
        result += albedo;
    } else if (numLights == 0) {
        result += ambientLight(vec3(0.7), albedo, metallic, roughness, F0, NdotV, occlusion);
    } else {
        result += ambientLight(sceneAmbient, albedo, metallic, roughness, F0, NdotV, occlusion);
        for (int i = 0; i < numLights; ++i) {

            Light light = lights[i];
            light.color = toLinear(light.color);

            vec3 lightDir;
            if (light.type == 0)
                lightDir = normalize(-light.direction);
            else
                lightDir = normalize(light.position - FragPos);

            vec3 ambient = ambientLight(light.color * light.ambient, albedo, metallic, roughness, F0, NdotV, occlusion);

            // The light's color * intensity is the radiance times PI: the Lambert term albedo / PI then gives
            // albedo * color * intensity * NdotL, the brightness the Blinn-Phong shading had, so scenes keep their exposure
            vec3 radiance = light.color * light.intensity * PI;
            float NdotL = max(dot(normal, lightDir), 0.0);
            vec3 halfDir = normalize(lightDir + viewDir);
            float NdotH = max(dot(normal, halfDir), 0.0);
            vec3 F = fresnelSchlick(max(dot(halfDir, viewDir), 0.0), F0);
            vec3 specular = distributionGGX(NdotH, a2) * visibilitySmithGGX(NdotL, NdotV, a2) * F;
            vec3 diffuse = (vec3(1.0) - F) * (1.0 - metallic) * albedo / PI;
            vec3 direct = (diffuse + specular) * radiance * NdotL;

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
                (1.0 - shadowFactor) * direct;

            lightResult *= attenuation * spotFactor;

            result += lightResult;
        }
    }

    if (unlit != 1)
        result += environmentLight(normal, viewDir, albedo, metallic, roughness, F0, NdotV, occlusion);

    if (fogMode != 0) {
        float dist = length(viewPos - FragPos);
        float fog = fogMode == 1 ? clamp((dist - fogStart) / (fogFinish - fogStart), 0.0, 1.0)
                                 : 1.0 - exp(-dist * dist * fogDensity * fogDensity);
        result = mix(result, fogColor, fog);
    }

    // Cut-out materials are opaque where they are not discarded
    float alpha = alphaMode == 1 ? 1.0 : diffuseSample.a;
    if (alphaToCoverage == 1)
        alpha = cutCoverage; // the draw is opaque: this alpha only sets the coverage of the samples
    FragColor = vec4(max(result, vec3(0.0)), alpha * opacity);
}
