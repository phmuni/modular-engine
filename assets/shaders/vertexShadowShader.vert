#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in mat4 aInstanceModel;

uniform mat4 lightSpaceMatrix;
uniform mat4 model;
uniform int useInstancing;

void main() {
    mat4 modelMatrix = useInstancing == 1 ? aInstanceModel : model;
    gl_Position = lightSpaceMatrix * modelMatrix * vec4(aPos, 1.0);
}
