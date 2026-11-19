#version 330 core
// Final pass: the display image (tone mapped scene and particles) plus the bloom, onto the screen

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D displayTexture;
uniform sampler2D bloomTexture;
uniform int bloomEnabled;
uniform float bloomStrength;
uniform int gammaCorrect; // the bloom is linear light: added in linear space, then encoded again

void main() {
    vec3 color = texture(displayTexture, TexCoords).rgb;
    if (bloomEnabled == 1) {
        vec3 bloom = texture(bloomTexture, TexCoords).rgb * bloomStrength;
        color = gammaCorrect == 1 ? pow(pow(color, vec3(2.2)) + bloom, vec3(1.0 / 2.2)) : color + bloom;
    }
    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
