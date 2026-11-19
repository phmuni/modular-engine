#version 330 core
// Skybox: the view direction of each pixel looks up an equirectangular panorama

in vec2 ndc;
out vec4 FragColor;

uniform sampler2D skyTexture;
uniform mat4 inverseViewProjection; // of the camera's rotation only: the sky does not move with the camera
uniform float intensity;
uniform float rotation; // radians around +Y
uniform int gammaCorrect;

const float PI = 3.14159265359;

void main() {
    vec4 world = inverseViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(world.xyz / world.w);
    float c = cos(rotation), s = sin(rotation);
    dir = vec3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z);

    // Longitude 0 (the image's center column) looks down -Z; the top row looks up
    vec2 uv = vec2(atan(dir.x, -dir.z) / (2.0 * PI) + 0.5, asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5);

    // atan jumps from 1 to 0 at the back seam: without this the mip level there would be the smallest one, a line
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    dx.x -= round(dx.x);
    dy.x -= round(dy.x);
    vec3 color = textureGrad(skyTexture, uv, dx, dy).rgb;

    if (gammaCorrect == 1)
        color = pow(color, vec3(2.2));
    FragColor = vec4(color * intensity, 1.0);
}
