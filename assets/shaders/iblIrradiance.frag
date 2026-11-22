#version 410 core
// Image-based lighting, step 2: the diffuse irradiance cube map. Each texel is the average of the environment over the
// cosine-weighted hemisphere around its direction (that is, irradiance / PI: the lit shader multiplies it by the
// albedo). The environment is read at a mip level matching each sample's solid angle, so a few samples do not show
// as noise.

in vec2 TexCoords;
out vec4 FragColor;

uniform samplerCube environment;
uniform int face;
uniform float environmentSize; // texels along an edge of the environment's level 0

const float PI = 3.14159265359;
const int SAMPLES = 128;

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
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    float texelSolidAngle = 4.0 * PI / (6.0 * environmentSize * environmentSize);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < SAMPLES; ++i) {
        vec2 xi = hammersley(i, SAMPLES);
        float phi = 2.0 * PI * xi.x;
        float cosTheta = sqrt(1.0 - xi.y);
        float sinTheta = sqrt(xi.y);
        vec3 L = right * (cos(phi) * sinTheta) + up * (sin(phi) * sinTheta) + N * cosTheta;
        float pdf = cosTheta / PI;
        float sampleSolidAngle = 1.0 / (float(SAMPLES) * pdf + 1e-6);
        float lod = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);
        sum += textureLod(environment, L, lod).rgb;
    }
    FragColor = vec4(sum / float(SAMPLES), 1.0);
}
