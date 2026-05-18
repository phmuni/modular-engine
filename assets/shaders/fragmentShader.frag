#version 330 core
out vec4 FragColor;

struct Material {
    sampler2D diffuse;
    sampler2D specular;
    // sampler2D normal; // TODO: re-enable when TBN is implemented
    sampler2D emission;
    float shininess;
    vec3 emissionColor;
    float emissionStrength;
}; 

struct Light {
    vec3 position;
    vec3 direction;
    vec3 color; 
    float intensity;
    float ambient;
    float constant;
    float linear;
    float quadratic;
    float cutOff;
    float outerCutOff;
    int type;
};

in vec3 FragPos;  
in vec3 Normal;  
in vec2 TexCoords;

uniform vec3 viewPos;
uniform Material material;
uniform int numLights;
uniform Light lights[10];

uniform sampler2D shadowMap;
uniform mat4 lightSpaceMatrix;
uniform int useShadows;
uniform vec3 shadowLightDir;
uniform float opacity;
uniform int useSolidDiffuseColor;
uniform vec3 solidDiffuseColor;

float calculateShadow(vec4 fragPosLightSpace)
{
    if (useShadows == 0) return 0.0;

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;

    vec3 nGeom   = normalize(Normal);
    vec3 lightDir = normalize(-shadowLightDir);

    float bias = max(0.01 * (1.0 - dot(nGeom, lightDir)), 0.001);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    int kernelSize = 3;
    int sampleCount = 0;

    for (int x = -kernelSize; x <= kernelSize; ++x) {
        for (int y = -kernelSize; y <= kernelSize; ++y) {
            vec2 offset = projCoords.xy + vec2(x, y) * texelSize;

            if (offset.x < 0.0 || offset.x > 1.0 ||
                offset.y < 0.0 || offset.y > 1.0)
                continue;

            float pcfDepth = texture(shadowMap, offset).r;

            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
            sampleCount++;
        }
    }

    if (sampleCount > 0)
        shadow /= float(sampleCount);

    return shadow;
}

void main()
{
    vec3 normal = normalize(Normal);

    vec3 viewDir = normalize(viewPos - FragPos);

    vec4 diffuseSample = useSolidDiffuseColor == 1 ? vec4(solidDiffuseColor, 1.0) : texture(material.diffuse, TexCoords);
    vec3 diffuseTex  = diffuseSample.rgb;
    vec3 specularTex = texture(material.specular, TexCoords).rgb;
    vec3 emissionTex = texture(material.emission, TexCoords).rgb;
    vec3 emissionResult = material.emissionColor * material.emissionStrength * emissionTex;

    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    float shadow = calculateShadow(fragPosLightSpace);

    vec3 result = emissionResult;

    if (numLights == 0) {
        result += diffuseTex * 0.7;
    } else {
        for (int i = 0; i < numLights; ++i) {

            Light light = lights[i];

            vec3 lightDir;
            if (light.type == 0)
                lightDir = normalize(-light.direction);
            else
                lightDir = normalize(light.position - FragPos);

            vec3 ambient = light.color * light.ambient * diffuseTex;

            float diff = max(dot(normal, lightDir), 0.0);
            vec3 diffuse = light.color * light.intensity * diff * diffuseTex;

            float shininess = max(material.shininess, 1.0);
            vec3 reflectDir = reflect(-lightDir, normal);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
            vec3 specular = light.color * light.intensity * spec * specularTex;

            float attenuation = 1.0;
            if (light.type == 1 || light.type == 2) {
                float dist = length(light.position - FragPos);
                attenuation = 1.0 /
                    (light.constant + light.linear * dist + light.quadratic * (dist * dist));
            }

            float spotFactor = 1.0;
            if (light.type == 2) {
                float theta = dot(lightDir, normalize(-light.direction));
                float epsilon = max(light.cutOff - light.outerCutOff, 1e-5);
                spotFactor = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
            }

            float shadowFactor = (light.type == 0) ? shadow : 0.0;

            vec3 lightResult =
                ambient +
                (1.0 - shadowFactor) * (diffuse + specular);

            lightResult *= attenuation * spotFactor;

            result += lightResult;
        }
    }

    FragColor = vec4(result, opacity);
}
