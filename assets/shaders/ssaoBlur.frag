#version 330 core
// Blurs the AO and brings it to the render size in one pass: 16 taps around the pixel (the noise pattern's 4x4 period,
// so the pattern averages out), each weighted by how close its depth is to the pixel's, so occlusion does not bleed
// across edges.

in vec2 TexCoords;
out float FragColor;

uniform sampler2D aoTexture;    // the raw AO, depthScale times smaller than the output
uniform sampler2D depthTexture; // the depth prepass, at the output's size
uniform vec4 projInfo;          // see ssao.frag
uniform int depthScale;

float viewZ(float depth) { return -projInfo.w / (depth * 2.0 - 1.0 + projInfo.z); }

void main() {
    ivec2 depthSize = textureSize(depthTexture, 0);
    ivec2 aoSize = textureSize(aoTexture, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(depthTexture, pixel, 0).r;
    if (depth >= 1.0) {
        FragColor = 1.0;
        return;
    }
    float z = viewZ(depth);
    float tolerance = 0.03 * abs(z) + 0.03;

    ivec2 base = pixel / depthScale;
    float sum = 0.0, weights = 0.0;
    for (int j = -1; j <= 2; ++j) {
        for (int i = -1; i <= 2; ++i) {
            ivec2 tap = base + ivec2(i, j);
            if (tap.x < 0 || tap.y < 0 || tap.x >= aoSize.x || tap.y >= aoSize.y)
                continue;
            ivec2 tapPixel = min(tap * depthScale, depthSize - 1);
            float tapZ = viewZ(texelFetch(depthTexture, tapPixel, 0).r);
            vec2 distance = abs(vec2(pixel - tapPixel)) / float(depthScale);
            float spatial = (1.0 - 0.2 * distance.x) * (1.0 - 0.2 * distance.y);
            float depthDifference = (tapZ - z) / tolerance;
            float weight = spatial * exp(-depthDifference * depthDifference) + 1e-4;
            sum += texelFetch(aoTexture, tap, 0).r * weight;
            weights += weight;
        }
    }
    FragColor = sum / weights;
}
