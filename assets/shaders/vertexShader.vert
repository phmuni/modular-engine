#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTex;

// Per-instance data, used when useInstancing == 1 (see Renderer::InstanceData)
layout(location = 3) in mat4 aInstanceModel;
layout(location = 7) in mat3 aInstanceNormal;
layout(location = 10) in vec4 aTangent; // xyz along +u, w the bitangent's sign

uniform mat4 MVP;
uniform mat4 model;
uniform mat3 normalMatrix;
uniform mat4 view;
uniform mat4 projection;
uniform int useInstancing;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 Tangent;

void main() {
    if (useInstancing == 1) {
        FragPos = vec3(aInstanceModel * vec4(aPos, 1.0));
        Normal = normalize(aInstanceNormal * aNormal);
        Tangent = vec4(mat3(aInstanceModel) * aTangent.xyz, aTangent.w);
        gl_Position = projection * view * vec4(FragPos, 1.0);
    } else {
        FragPos = vec3(model * vec4(aPos, 1.0));
        Normal = normalize(normalMatrix * aNormal);
        Tangent = vec4(mat3(model) * aTangent.xyz, aTangent.w);
        gl_Position = MVP * vec4(aPos, 1.0);
    }
    TexCoords = aTex;
}
