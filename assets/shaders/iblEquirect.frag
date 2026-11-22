#version 410 core
// Image-based lighting, step 1: one face of the environment cube map, from the equirectangular panorama (the same
// mapping skybox.frag draws: the image's center column looks down -Z, its top row up)

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D panorama;
uniform int face;        // 0-5: +X, -X, +Y, -Y, +Z, -Z
uniform int sourceEncoding; // 0: linear light (an HDR image), 1: an 8-bit image used as written, 2: 8-bit, to linear

const float PI = 3.14159265359;

// The direction a texel of a cube face looks along (the cube map's own orientation rules)
vec3 faceDirection(int face, vec2 uv)
{
    float sc = uv.x * 2.0 - 1.0;
    float tc = uv.y * 2.0 - 1.0;
    vec3 d;
    if (face == 0) d = vec3(1.0, -tc, -sc);
    else if (face == 1) d = vec3(-1.0, -tc, sc);
    else if (face == 2) d = vec3(sc, 1.0, tc);
    else if (face == 3) d = vec3(sc, -1.0, -tc);
    else if (face == 4) d = vec3(sc, -tc, 1.0);
    else d = vec3(-sc, -tc, -1.0);
    return normalize(d);
}

void main()
{
    vec3 dir = faceDirection(face, TexCoords);
    vec2 uv = vec2(atan(dir.x, -dir.z) / (2.0 * PI) + 0.5, asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5);
    // atan jumps from 1 to 0 at the back seam: without this the mip level there would be the smallest one
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    dx.x -= round(dx.x);
    dy.x -= round(dy.x);
    vec3 color = textureGrad(panorama, uv, dx, dy).rgb;
    if (sourceEncoding == 2)
        color = pow(color, vec3(2.2));
    FragColor = vec4(min(color, vec3(60000.0)), 1.0); // a half float holds up to 65504
}
