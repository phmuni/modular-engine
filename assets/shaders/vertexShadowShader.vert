#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in mat4 aInstanceModel;
layout(location = 11) in vec4 aJoints;
layout(location = 12) in vec4 aWeights;
layout(location = 13) in int aJointOffset; // where the instance's joints start in the joint buffer, -1 for none

uniform mat4 lightSpaceMatrix;
uniform mat4 model;
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

#ifdef CUSTOM_SURFACE
// The variant for parts with a custom surface shader (see customShader.h): the file's vertex() moves the vertex as it
// does in the lit shader, so waves and the like show in the shadow. Such parts are drawn one at a time, never
// instanced, with the matrix normalMatrix goes with.
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTex;
uniform mat3 normalMatrix;
uniform float time; // seconds of game time
struct VertexData {
    vec3 position;      // world space; change it to move the vertex
    vec3 normal;        // world space
    vec2 uv;
    vec3 localPosition; // in the model's own space (read only)
};
out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
//@CUSTOM_SURFACE_CODE
#endif

void main() {
    mat4 skin = mat4(1.0);
    int firstJoint = useInstancing == 1 ? aJointOffset : jointOffset;
    if (firstJoint >= 0)
        skin = skinMatrix(firstJoint, aJoints, aWeights);
    mat4 modelMatrix = useInstancing == 1 ? aInstanceModel : model;
#ifdef CUSTOM_SURFACE
    VertexData vertexData;
    vertexData.position = vec3(modelMatrix * skin * vec4(aPos, 1.0));
    vertexData.normal = normalize(normalMatrix * (mat3(skin) * aNormal));
    vertexData.uv = aTex;
    vertexData.localPosition = aPos;
#ifdef CUSTOM_HAS_VERTEX
    vertex(vertexData);
#endif
    FragPos = vertexData.position;
    Normal = normalize(vertexData.normal);
    TexCoords = vertexData.uv;
    gl_Position = lightSpaceMatrix * vec4(FragPos, 1.0);
#else
    gl_Position = lightSpaceMatrix * modelMatrix * skin * vec4(aPos, 1.0);
#endif
}
