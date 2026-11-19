#version 330 core
// Bloom, first half: each pass halves the image with a 13-tap filter (Jimenez, "Next Generation Post Processing in
// Call of Duty: Advanced Warfare"), which keeps small bright spots from flickering as they move.

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D sourceTexture;
uniform vec2 sourceTexel;   // one texel of the source
uniform int prefilter;      // the first pass keeps only what is brighter than the threshold
uniform float threshold;
uniform float knee;         // softens the threshold, so highlights fade in rather than switch on

vec3 tap(vec2 offset) { return texture(sourceTexture, TexCoords + offset * sourceTexel).rgb; }

vec3 thresholded(vec3 color) {
    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-5);
    return color * contribution;
}

void main() {
    vec3 a = tap(vec2(-2.0, 2.0)), b = tap(vec2(0.0, 2.0)), c = tap(vec2(2.0, 2.0));
    vec3 d = tap(vec2(-2.0, 0.0)), e = tap(vec2(0.0, 0.0)), f = tap(vec2(2.0, 0.0));
    vec3 g = tap(vec2(-2.0, -2.0)), h = tap(vec2(0.0, -2.0)), i = tap(vec2(2.0, -2.0));
    vec3 j = tap(vec2(-1.0, 1.0)), k = tap(vec2(1.0, 1.0));
    vec3 l = tap(vec2(-1.0, -1.0)), m = tap(vec2(1.0, -1.0));

    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (prefilter == 1)
        color = thresholded(color);
    FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
