#version 410 core
// Image-based lighting: the split-sum environment BRDF lookup table. x = NdotV, y = roughness; the result is the scale
// and bias applied to F0 (specular reflectance = F0 * r + g). Made once per run.

in vec2 TexCoords;
out vec4 FragColor;

const float PI = 3.14159265359;
const int SAMPLES = 128;

vec2 hammersley(int i, int count)
{
    return vec2(float(i) / float(count), float(bitfieldReverse(uint(i))) * 2.3283064365386963e-10);
}

void main()
{
    float NdotV = max(TexCoords.x, 1e-3);
    float roughness = max(TexCoords.y, 0.02);
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    float a = roughness * roughness;
    float a2 = a * a;
    float A = 0.0;
    float B = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        vec2 xi = hammersley(i, SAMPLES);
        float phi = 2.0 * PI * xi.x;
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        vec3 L = 2.0 * dot(V, H) * H - V;
        float NdotL = clamp(L.z, 0.0, 1.0);
        float NdotH = clamp(H.z, 0.0, 1.0);
        float VdotH = clamp(dot(V, H), 0.0, 1.0);
        if (NdotL > 0.0) {
            // Smith-GGX visibility (the height-correlated form the lit shader uses), over the sampling density
            float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
            float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
            float vis = 0.5 / max(gv + gl, 1e-5);
            float weight = vis * NdotL * 4.0 * VdotH / max(NdotH, 1e-4);
            float fresnel = pow(1.0 - VdotH, 5.0);
            A += (1.0 - fresnel) * weight;
            B += fresnel * weight;
        }
    }
    FragColor = vec4(A / float(SAMPLES), B / float(SAMPLES), 0.0, 1.0);
}
