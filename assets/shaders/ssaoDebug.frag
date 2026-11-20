#version 330 core
// The AO alone, as grey (white: nothing occludes), for tuning it in the editor.

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D aoTexture;

void main() {
    FragColor = vec4(vec3(texture(aoTexture, TexCoords).r), 1.0);
}
