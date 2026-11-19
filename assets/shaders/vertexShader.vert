#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTex;

// Per-instance data, used when useInstancing == 1 (see Renderer::InstanceData)
layout(location = 3) in mat4 aInstanceModel;
layout(location = 7) in mat3 aInstanceNormal;
layout(location = 10) in vec4 aTangent; // xyz along +u, w the bitangent's sign
// Skinned models (useSkinning == 1): up to four joints per vertex and their weights
layout(location = 11) in vec4 aJoints;
layout(location = 12) in vec4 aWeights;

const int MAX_JOINTS = 128; // EngineConfig::MAX_JOINTS

uniform mat4 MVP;
uniform mat4 model;
uniform mat3 normalMatrix;
uniform mat4 view;
uniform mat4 projection;
uniform int useInstancing;
uniform int useSkinning;
uniform mat4 bones[MAX_JOINTS];

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 Tangent;

void main() {
    // Skinning: the vertex moved by its joints' matrices, in the model's space
    mat4 skin = mat4(1.0);
    if (useSkinning == 1)
        skin = aWeights.x * bones[int(aJoints.x)] + aWeights.y * bones[int(aJoints.y)] +
               aWeights.z * bones[int(aJoints.z)] + aWeights.w * bones[int(aJoints.w)];
    vec4 position = skin * vec4(aPos, 1.0);
    vec3 normal = mat3(skin) * aNormal;
    vec3 tangent = mat3(skin) * aTangent.xyz;

    if (useInstancing == 1) {
        FragPos = vec3(aInstanceModel * position);
        Normal = normalize(aInstanceNormal * normal);
        Tangent = vec4(mat3(aInstanceModel) * tangent, aTangent.w);
        gl_Position = projection * view * vec4(FragPos, 1.0);
    } else {
        FragPos = vec3(model * position);
        Normal = normalize(normalMatrix * normal);
        Tangent = vec4(mat3(model) * tangent, aTangent.w);
        gl_Position = MVP * position;
    }
    TexCoords = aTex;
}
