#version 330 core
// Screen-space ambient occlusion from the depth prepass: a hemisphere of samples around each visible point, oriented
// by the normal rebuilt from the depth, counts how many of them are behind a nearer surface (see ssao.h).

in vec2 TexCoords;
out float FragColor;

uniform sampler2D depthTexture;
uniform sampler2D noiseTexture; // 4x4 random rotations around the normal, repeated over the screen
uniform vec3 kernel[32];        // samples in the +Z hemisphere, denser near the center
uniform int kernelSize;
uniform vec4 projInfo;          // 1 / projection[0][0], 1 / projection[1][1], projection[2][2], projection[3][2]
uniform mat4 projection;
uniform int depthScale;         // this pass may be smaller than the depth texture, by this factor
uniform float radius;
uniform float intensity;

float viewZ(float depth) { return -projInfo.w / (depth * 2.0 - 1.0 + projInfo.z); }

vec3 viewPosAt(ivec2 pixel, ivec2 depthSize) {
    pixel = clamp(pixel, ivec2(0), depthSize - 1);
    float z = viewZ(texelFetch(depthTexture, pixel, 0).r);
    vec2 ndc = (vec2(pixel) + 0.5) / vec2(depthSize) * 2.0 - 1.0;
    return vec3(ndc * projInfo.xy * -z, z);
}

void main() {
    ivec2 depthSize = textureSize(depthTexture, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy) * depthScale;
    float depth = texelFetch(depthTexture, clamp(pixel, ivec2(0), depthSize - 1), 0).r;
    if (depth >= 1.0) { // the sky
        FragColor = 1.0;
        return;
    }

    vec3 position = viewPosAt(pixel, depthSize);
    // The normal from the neighbors on the nearer side of each axis, so it holds at silhouette edges
    vec3 left = viewPosAt(pixel + ivec2(-1, 0), depthSize), right = viewPosAt(pixel + ivec2(1, 0), depthSize);
    vec3 down = viewPosAt(pixel + ivec2(0, -1), depthSize), up = viewPosAt(pixel + ivec2(0, 1), depthSize);
    bool useRight = abs(right.z - position.z) < abs(position.z - left.z);
    bool useUp = abs(up.z - position.z) < abs(position.z - down.z);
    if (pixel.x == 0) useRight = true; // at the screen's edges only one neighbor exists
    if (pixel.x >= depthSize.x - 1) useRight = false;
    if (pixel.y == 0) useUp = true;
    if (pixel.y >= depthSize.y - 1) useUp = false;
    vec3 dx = useRight ? right - position : position - left;
    vec3 dy = useUp ? up - position : position - down;
    vec3 normal = normalize(cross(dx, dy));

    vec3 random = vec3(texelFetch(noiseTexture, ivec2(gl_FragCoord.xy) & 3, 0).xy, 0.0);
    vec3 tangent = normalize(random - normal * dot(random, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 basis = mat3(tangent, bitangent, normal);

    float bias = 0.025 + 0.002 * abs(position.z); // depth gets coarser with distance
    float occlusion = 0.0;
    for (int i = 0; i < kernelSize; ++i) {
        vec3 sampleView = position + basis * kernel[i] * radius;
        vec4 clip = projection * vec4(sampleView, 1.0);
        if (clip.w <= 0.0)
            continue;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            continue;
        float sampledZ =
            viewZ(texelFetch(depthTexture, clamp(ivec2(uv * vec2(depthSize)), ivec2(0), depthSize - 1), 0).r);
        // A surface that is much nearer than the point is not what shades it
        float range = smoothstep(0.0, 1.0, radius / abs(position.z - sampledZ));
        occlusion += (sampledZ >= sampleView.z + bias ? 1.0 : 0.0) * range;
    }
    FragColor = pow(clamp(1.0 - occlusion / float(kernelSize), 0.0, 1.0), intensity);
}
