#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTex;

// Per-instance data, used when useInstancing == 1 (see Renderer::InstanceData)
layout(location = 3) in mat4 aInstanceModel;
layout(location = 7) in mat3 aInstanceNormal;
layout(location = 10) in vec4 aTangent; // xyz along +u, w the bitangent's sign
// Skinned models: up to four joints per vertex and their weights
layout(location = 11) in vec4 aJoints;
layout(location = 12) in vec4 aWeights;
// Where the instance's joints start in the joint buffer, -1 when it is not skinned
layout(location = 13) in int aJointOffset;

uniform mat4 MVP;
uniform mat4 model;
uniform mat3 normalMatrix;
uniform mat4 view;
uniform mat4 projection;
uniform int useInstancing;
uniform int jointOffset = -1; // the same for a draw that is not instanced

// Skinning: the joints' matrices come from the joint buffer, three texels (the rows of an affine matrix) per joint
uniform samplerBuffer jointData;

mat4 skinMatrix(int firstJoint, vec4 joints, vec4 weights) {
    vec4 r0 = vec4(0.0), r1 = vec4(0.0), r2 = vec4(0.0);
    for (int i = 0; i < 4; ++i) {
        int base = (firstJoint + int(joints[i])) * 3;
        r0 += weights[i] * texelFetch(jointData, base);
        r1 += weights[i] * texelFetch(jointData, base + 1);
        r2 += weights[i] * texelFetch(jointData, base + 2);
    }
    return transpose(mat4(r0, r1, r2, vec4(0.0, 0.0, 0.0, weights.x + weights.y + weights.z + weights.w)));
}

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 Tangent;

#ifdef CUSTOM_SURFACE
// A custom surface shader (see README, "Custom surface shaders"): its code goes in at the marker, and vertex() moves
// the vertex after skinning and the model matrix, in world space
uniform float time; // seconds of game time
struct VertexData {
    vec3 position;      // world space; change it to move the vertex
    vec3 normal;        // world space
    vec2 uv;
    vec3 localPosition; // in the model's own space (read only)
};
//@CUSTOM_SURFACE_CODE
#endif

void main() {
    // Skinning: the vertex moved by its joints' matrices, in the model's space
    mat4 skin = mat4(1.0);
    int firstJoint = useInstancing == 1 ? aJointOffset : jointOffset;
    if (firstJoint >= 0)
        skin = skinMatrix(firstJoint, aJoints, aWeights);
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

#ifdef CUSTOM_SURFACE
    VertexData vertexData;
    vertexData.position = FragPos;
    vertexData.normal = Normal;
    vertexData.uv = aTex;
    vertexData.localPosition = aPos;
#ifdef CUSTOM_HAS_VERTEX
    vertex(vertexData);
#endif
    FragPos = vertexData.position;
    Normal = normalize(vertexData.normal);
    TexCoords = vertexData.uv;
    gl_Position = projection * view * vec4(FragPos, 1.0);
#endif
}
