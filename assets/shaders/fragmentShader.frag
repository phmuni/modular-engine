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

uniform sampler2DShadow shadowMap; // hardware depth comparison with bilinear filtering
uniform mat4 lightSpaceMatrix;
uniform int useShadows;
uniform vec3 shadowLightDir;
uniform float opacity;
uniform int useSolidDiffuseColor;
uniform vec3 solidDiffuseColor;

// glTF-style material inputs (see AlphaMode)
uniform vec4 baseColorFactor; // tints the diffuse texture; alpha multiplies its alpha
uniform int alphaMode;        // 0 opaque, 1 mask, 2 blend
uniform float alphaCutoff;
uniform int doubleSided;

float calculateShadow(vec4 fragPosLightSpace)
{
    if (useShadows == 0) return 0.0;

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;

    vec3 nGeom   = normalize(Normal);
    vec3 lightDir = normalize(-shadowLightDir);
    
    float bias = max(0.005 * (1.0 - dot(nGeom, lightDir)), 0.0005);

    float lit = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    // 3x3 PCF: each tap is already a filtered 2x2 comparison, so this covers about the same area as a manual 5x5.
    // Taps outside the map read the border depth (1.0) and count as lit.
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize * 1.5;
            lit += texture(shadowMap, vec3(projCoords.xy + offset, currentDepth - bias));
        }
    }

    return 1.0 - lit / 9.0;
}

void main()
{
    vec3 normal = normalize(Normal);
    // Back faces of double-sided materials are lit as the front
    if (doubleSided == 1 && !gl_FrontFacing)
        normal = -normal;

    vec3 viewDir = normalize(viewPos - FragPos);

    // Sampled unconditionally: texture lookups in a branch get unreliable derivatives at triangle edges
    vec4 diffuseTexel = texture(material.diffuse, TexCoords);
    vec4 diffuseSample = useSolidDiffuseColor == 1 ? vec4(solidDiffuseColor, 1.0)
                                                   : diffuseTexel * vec4(baseColorFactor.rgb, 1.0);
    diffuseSample.a *= baseColorFactor.a;
#ifdef ALPHA_MASK
    // Only in the mask variant: any discard in a shader turns off hidden surface removal on tile-based GPUs,
    // which slows down every opaque draw that uses it
    if (diffuseSample.a < alphaCutoff)
        discard;
#endif
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

    // Cut-out materials are opaque where they are not discarded
    float alpha = alphaMode == 1 ? 1.0 : diffuseSample.a;
    FragColor = vec4(result, alpha * opacity);
}
