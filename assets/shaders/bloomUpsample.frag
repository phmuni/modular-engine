#version 330 core
// Bloom, second half: each pass enlarges the smaller level with a 3x3 tent filter and adds it (additive blending)
// onto the next larger one, so the glow spreads wide while staying smooth.

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D sourceTexture;
uniform vec2 sourceTexel;

void main() {
    vec2 t = sourceTexel;
    vec3 color = texture(sourceTexture, TexCoords).rgb * 4.0;
    color += (texture(sourceTexture, TexCoords + vec2(-t.x, 0.0)).rgb + texture(sourceTexture, TexCoords + vec2(t.x, 0.0)).rgb +
              texture(sourceTexture, TexCoords + vec2(0.0, -t.y)).rgb + texture(sourceTexture, TexCoords + vec2(0.0, t.y)).rgb) * 2.0;
    color += texture(sourceTexture, TexCoords + vec2(-t.x, -t.y)).rgb + texture(sourceTexture, TexCoords + vec2(t.x, -t.y)).rgb +
             texture(sourceTexture, TexCoords + vec2(-t.x, t.y)).rgb + texture(sourceTexture, TexCoords + vec2(t.x, t.y)).rgb;
    FragColor = vec4(color / 16.0, 1.0);
}
