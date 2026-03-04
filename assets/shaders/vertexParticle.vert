#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;

uniform mat4 view;
uniform mat4 projection;

out vec4 ParticleColor;

void main() {
    ParticleColor = aColor;
    vec4 eyePos = view * vec4(aPos, 1.0);
    gl_Position = projection * eyePos;
    // Scale point size by distance to camera
    gl_PointSize = max(aSize * (300.0 / -eyePos.z), 1.0);
}
