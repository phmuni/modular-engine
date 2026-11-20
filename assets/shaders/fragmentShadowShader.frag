#version 330 core

#ifdef CUSTOM_SURFACE
// The variant for parts with a custom surface shader (see customShader.h): only what surface() does to the alpha
// matters here, so a `discard` or a low alpha leaves a hole in the shadow (a low alpha only in files without
// `#pragma blend`: a see-through surface keeps its shadow). The material's textures are not read in this pass: the
// surface starts with a white albedo and an alpha of 1, and `viewDir` points at the camera.
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
uniform vec3 viewPos;
uniform float time; // seconds of game time
struct Surface {
    vec3 albedo;
    float alpha;
    vec3 emission;
    vec3 specular;
    float shininess;
    vec3 normal;
    vec2 uv;
    vec3 worldPos;
    vec3 viewDir;
};
// Shadow maps are single-sample: the cut is a plain discard (see cutBelow in fragmentShader.frag)
void cutBelow(float value, float threshold) {
    if (value < threshold)
        discard;
}
//@CUSTOM_SURFACE_CODE
#endif

void main() {
#if defined(CUSTOM_SURFACE) && defined(CUSTOM_HAS_SURFACE)
    Surface surf;
    surf.albedo = vec3(1.0);
    surf.alpha = 1.0;
    surf.emission = vec3(0.0);
    surf.specular = vec3(0.0);
    surf.shininess = 16.0;
    surf.normal = normalize(Normal);
    surf.uv = TexCoords;
    surf.worldPos = FragPos;
    surf.viewDir = normalize(viewPos - FragPos);
    surface(surf);
#ifndef CUSTOM_BLEND
    if (surf.alpha < 0.5)
        discard;
#endif
#endif
}
