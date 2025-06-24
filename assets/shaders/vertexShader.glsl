#version 330 core

layout(location = 0) in vec3 aPos;     // Vertice position
layout(location = 1) in vec3 aNormal;  // Vertice Normal position
layout(location = 2) in vec2 aTex;  // Vertice Texture position

uniform mat4 MVP;   // Matrix Model-View-Project
uniform mat4 model; // Matrix model with obj transformations

out vec3 FragPos;   // Frag position in world space
out vec3 Normal;    // Interpolated normal
out vec2 TexCoords;    // Texture coordinates

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = normalize(mat3(transpose(inverse(model))) * aNormal);
    TexCoords = aTex;
    gl_Position = MVP * vec4(aPos, 1.0);
}
