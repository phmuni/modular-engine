#include "model/material.h"

Material::Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission,
                   float shininess)
    : diffuseMap(diffuse), specularMap(specular), normalMap(normal),
      emissionMap(emission), shininess(shininess) {}

GLuint Material::getDiffuseMap() const { return diffuseMap; }

GLuint Material::getSpecularMap() const { return specularMap; }

GLuint Material::getNormalMap() const { return normalMap; }

GLuint Material::getEmissionMap() const { return emissionMap; }

float Material::getShininess() const { return shininess; }
