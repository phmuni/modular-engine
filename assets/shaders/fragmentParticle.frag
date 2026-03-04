#version 330 core

in vec4 ParticleColor;
out vec4 FragColor;

void main() {
    // Make point sprite circular
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);
    if (dist > 0.5)
        discard;

    // Soft edge falloff
    float alpha = smoothstep(0.5, 0.2, dist);
    FragColor = vec4(ParticleColor.rgb, ParticleColor.a * alpha);
}
