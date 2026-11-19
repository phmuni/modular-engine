#version 330 core
// The HDR scene, tone mapped and converted for display. Particles then draw on top in display space, where their
// colors were tuned, and composite.frag adds the bloom.

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D sceneTexture;
// With gamma-correct lighting the scene is linear: compress its highlights into range, then encode for display.
// Without it (the old look), the scene is already display values and only gets clamped.
uniform int gammaCorrect;

// The highlight roll-off of Khronos PBR Neutral: colors below startCompression pass unchanged, so scenes keep their
// look, and brighter light compresses smoothly (keeping its hue) instead of clipping to flat white. Its toe, which
// darkens the shadows of brightly exposed scenes, is left out: this engine's scenes are mostly dim.
vec3 neutralTonemap(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

void main() {
    vec3 color = texture(sceneTexture, TexCoords).rgb;
    if (gammaCorrect == 1)
        color = pow(neutralTonemap(max(color, vec3(0.0))), vec3(1.0 / 2.2));
    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
