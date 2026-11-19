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

void main() {
    mat4 skin = mat4(1.0);
    if (useSkinning == 1)
        skin = aWeights.x * bones[int(aJoints.x)] + aWeights.y * bones[int(aJoints.y)] +
               aWeights.z * bones[int(aJoints.z)] + aWeights.w * bones[int(aJoints.w)];
    mat4 modelMatrix = useInstancing == 1 ? aInstanceModel : model;
    gl_Position = lightSpaceMatrix * modelMatrix * skin * vec4(aPos, 1.0);
}
