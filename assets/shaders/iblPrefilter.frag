#version 410 core
// Image-based lighting, step 3: one mip level of the pre-filtered specular cube map, for one roughness: the environment
// blurred by the GGX lobe of that roughness (GGX importance sampling, with N = V = R). Samples read the environment at
// a mip level that matches their solid angle, which keeps small bright lights from leaving fireflies.

in vec2 TexCoords;
out vec4 FragColor;

uniform samplerCube environment;
uniform int face;
uniform float roughness;
uniform float environmentSize; // texels along an edge of the environment's level 0
uniform float targetSize;      // texels along an edge of the level being drawn

const float PI = 3.14159265359;
const int SAMPLES = 64;

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

vec2 hammersley(int i, int count)
{
    return vec2(float(i) / float(count), float(bitfieldReverse(uint(i))) * 2.3283064365386963e-10);
}

void main()
{
    vec3 N = faceDirection(face, TexCoords);
    if (roughness < 0.001) {
        // A mirror: the environment itself, from the mip level that matches this level's texels
        FragColor = vec4(textureLod(environment, N, max(log2(environmentSize / targetSize), 0.0)).rgb, 1.0);
        return;
    }
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    float a = roughness * roughness;
    float a2 = a * a;
    float texelSolidAngle = 4.0 * PI / (6.0 * environmentSize * environmentSize);
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        vec2 xi = hammersley(i, SAMPLES);
        // A half vector from the GGX distribution, in the frame around N
        float phi = 2.0 * PI * xi.x;
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 H = right * (cos(phi) * sinTheta) + up * (sin(phi) * sinTheta) + N * cosTheta;
        vec3 L = normalize(2.0 * dot(N, H) * H - N);
        float NdotL = dot(N, L);
        if (NdotL > 0.0) {
            float d = cosTheta * cosTheta * (a2 - 1.0) + 1.0;
            float D = a2 / (PI * d * d);
            float pdf = D / 4.0; // D * NdotH / (4 * VdotH) with V = N
            float sampleSolidAngle = 1.0 / (float(SAMPLES) * pdf + 1e-6);
            float lod = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);
            sum += textureLod(environment, L, lod).rgb * NdotL;
            weight += NdotL;
        }
    }
    FragColor = vec4(sum / max(weight, 1e-4), 1.0);
}
