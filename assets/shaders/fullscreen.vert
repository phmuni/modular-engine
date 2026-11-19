#version 330 core
// One triangle that covers the screen, from gl_VertexID alone (no vertex buffer)

out vec2 TexCoords;

void main() {
    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    TexCoords = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
