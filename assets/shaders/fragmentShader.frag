#version 330 core
out vec4 FragColor;

// Material properties for per-fragment shading
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
  int type; // 0 = directional, 1 = point, 2 = spotlight
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
uniform int useShadows;

float calculateShadow(vec4 fragPosLightSpace) {
    if (useShadows == 0) return 0.0;
    
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = 0.005;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    // Percentage Closer Filtering (PCF) 5x5: 25 samples for smooth shadow edges
    // Reduces shadow aliasing by averaging neighboring depth comparisons
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }

    shadow /= 25.0;
    return shadow;
}


void main() {
  vec3 norm = normalize(Normal);
  vec3 viewDir = normalize(viewPos - FragPos);

  vec3 diffuseTex = texture(material.diffuse, TexCoords).rgb;
  vec3 specularTex = texture(material.specular, TexCoords).rgb;
  vec3 emissionTex = texture(material.emission, TexCoords).rgb;

  vec4 fragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
  float shadow = calculateShadow(fragPosLightSpace);

  vec3 result = emissionTex;

  if (numLights == 0) {
    result += diffuseTex * 0.7;
  } else {
    for (int i = 0; i < numLights; ++i) {
      Light light = lights[i];

      vec3 lightDir;
      if (light.type == 0) {
        lightDir = normalize(-light.direction);
      } else {
        lightDir = normalize(light.position - FragPos);
      }

      vec3 ambient = light.color * light.ambient * diffuseTex;
      float diff = max(dot(norm, lightDir), 0.0);
      vec3 diffuse = light.color * light.intensity * diff * diffuseTex;

      vec3 reflectDir = reflect(-lightDir, norm);
      float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
      vec3 specular = light.color * light.intensity * spec * specularTex;

      float attenuation = 1.0;
      if (light.type == 1 || light.type == 2) {
        float distance = length(light.position - FragPos);
        attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));
      }

      float spotlightEffect = 1.0;
      if (light.type == 2) {
        float theta = dot(lightDir, normalize(-light.direction));
        float epsilon = max(light.cutOff - light.outerCutOff, 0.001);
        spotlightEffect = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
      }

      float shadowFactor = (light.type == 0) ? shadow : 0.0;
      vec3 lightResult = (ambient + (1.0 - shadowFactor) * (diffuse + specular)) * attenuation * spotlightEffect;
      result += lightResult;
    }
  }

  FragColor = vec4(result, 1.0);
}
