#version 330 core
out vec4 FragColor;

struct Material {
  sampler2D diffuse;
  sampler2D specular;    
  sampler2D emission;
  float shininess;
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
  int type; // 0 = point, 1 = directional, 2 = spotlight
};

in vec3 FragPos;  
in vec3 Normal;  
in vec2 TexCoords;

uniform vec3 viewPos;
uniform Material material;
uniform int numLights;
uniform Light lights[10];

// Shadow mapping
uniform sampler2D shadowMap;
uniform mat4 lightSpaceMatrix;

// Calcula sombra a partir da posição do fragmento no espaço da luz
float calculateShadow(vec4 fragPosLightSpace) {
    // Transformação para NDC [0,1]
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    // Se estiver fora do shadow map, não projeta sombra
    if (projCoords.z > 1.0)
        return 0.0;

    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;

    // Bias para evitar acne de sombra (self-shadowing)
    float bias = max(0.005 * (1.0 - dot(normalize(Normal), normalize(vec3(lightSpaceMatrix[0][2], lightSpaceMatrix[1][2], lightSpaceMatrix[2][2])))), 0.0005);

    // Shadow: 1.0 = na sombra, 0.0 = iluminado
    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    return shadow;
}

void main()
{
  vec3 result = vec3(0.0);
  vec3 norm = normalize(Normal);
  vec3 viewDir = normalize(viewPos - FragPos);

  vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
  float shadow = calculateShadow(fragPosLightSpace);

  for (int i = 0; i < numLights; ++i) {
    Light light = lights[i];

    // ambient
    vec3 ambient = light.color * light.ambient * texture(material.diffuse, TexCoords).rgb;

    // light direction
    vec3 lightDir;
    if (light.type == 0 || light.type == 2) {
      lightDir = normalize(light.position - FragPos);
    } else { // Directional
      lightDir = normalize(-light.direction);
    }

    // diffuse
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = light.color * light.intensity * diff * texture(material.diffuse, TexCoords).rgb;

    // specular
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specularShine = light.color * light.intensity * spec * texture(material.specular, TexCoords).rgb;

    // attenuation
    float attenuation = 1.0;
    if (light.type == 0 || light.type == 2) {
      float distance = length(light.position - FragPos);
      attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));
    }

    // spotlight cutoff
    float spotlightEffect = 1.0;
    if (light.type == 2) {
      float theta = dot(lightDir, normalize(-light.direction));
      float epsilon = light.cutOff - light.outerCutOff;
      spotlightEffect = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    }

    // Aplica sombra somente para luz direcional [0 = point, 1 = directional, 2 = spotlight]
    float shadowFactor = (light.type == 1) ? shadow : 0.0;

    // Resultado com sombra aplicada
    vec3 lightResult = (ambient + (1.0 - shadowFactor) * (diffuse + specularShine)) * attenuation * spotlightEffect;
    result += lightResult;
  }

  // Emission
  vec3 specularTexture = texture(material.specular, TexCoords).rgb;
  float mask = float(all(equal(specularTexture, vec3(0.0))));
  vec3 emissionTexture = texture(material.emission, TexCoords).rgb * mask;
  result += emissionTexture;

  FragColor = vec4(result, 1.0);
}
