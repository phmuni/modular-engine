#version 330 core
// One triangle that covers the screen at the far plane (depth 1), so the skybox shows only where nothing was drawn

out vec2 ndc;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    ndc = position;
    gl_Position = vec4(position, 1.0, 1.0);
}
