#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in mat4 aInstanceModel;
layout(location = 11) in vec4 aJoints;
layout(location = 12) in vec4 aWeights;

const int MAX_JOINTS = 128; // EngineConfig::MAX_JOINTS

uniform mat4 lightSpaceMatrix;
uniform mat4 model;
uniform int useInstancing;
uniform int useSkinning;
uniform mat4 bones[MAX_JOINTS];

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
    if (useSkinning == 1)
        skin = aWeights.x * bones[int(aJoints.x)] + aWeights.y * bones[int(aJoints.y)] +
               aWeights.z * bones[int(aJoints.z)] + aWeights.w * bones[int(aJoints.w)];
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
