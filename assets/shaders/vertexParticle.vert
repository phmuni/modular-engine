#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;

uniform mat4 view;
uniform mat4 projection;

// Point-mass gravitational lens (see GravityLens). Particles are drawn twice when it is active:
// instance 0 is the primary image, instance 1 the secondary image on the opposite side of the lens.
uniform int lensActive;
uniform vec3 lensPosition;
uniform float lensRadius; // Schwarzschild radius
uniform float lensMaxMagnification;

// The scene's fog (see fragmentShader.frag). Particles are drawn after tone mapping, where the fog's color is not
// theirs to mix toward, so they fade out into the fogged scene behind them instead.
uniform int fogMode; // 0 off, 1 linear, 2 exponential
uniform float fogStart;
uniform float fogFinish;
uniform float fogDensity;

out vec4 ParticleColor;

void main() {
    vec4 eyePos = view * vec4(aPos, 1.0);
    float magnification = 1.0;

    if (lensActive == 1) {
        vec3 lensEye = (view * vec4(lensPosition, 1.0)).xyz;
        float lensDepth = -lensEye.z;
        float sourceDepth = -eyePos.z;

        if (lensDepth > 0.0 && sourceDepth > lensDepth) {
            // Source position projected onto the lens plane, relative to the lens
            vec2 beta = eyePos.xy * (lensDepth / sourceDepth) - lensEye.xy;
            float b = max(length(beta), 1e-4);

            // Thin-lens Einstein radius on the lens plane: grows with the distance behind the lens
            float einstein2 = 2.0 * lensRadius * (sourceDepth - lensDepth) * lensDepth / sourceDepth;
            float root = sqrt(b * b + 4.0 * einstein2);
            float theta = gl_InstanceID == 0 ? 0.5 * (b + root) : 0.5 * (b - root);

            float u = b / max(sqrt(einstein2), 1e-4);
            float base = (u * u + 2.0) / (2.0 * u * sqrt(u * u + 4.0));
            magnification = min(gl_InstanceID == 0 ? base + 0.5 : base - 0.5, lensMaxMagnification);

            // Back from the lens plane to the particle's own depth, so depth testing still works
            eyePos.xy = (lensEye.xy + (beta / b) * theta) * (sourceDepth / lensDepth);
        } else if (gl_InstanceID != 0) {
            // Not behind the lens: there is no secondary image
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            gl_PointSize = 1.0;
            ParticleColor = vec4(0.0);
            return;
        }
    }

    // Lensing keeps surface brightness: magnified images grow, demagnified ones fade
    float fade = 1.0;
    if (fogMode != 0) {
        float dist = length(eyePos.xyz);
        float fog = fogMode == 1 ? clamp((dist - fogStart) / (fogFinish - fogStart), 0.0, 1.0)
                                 : 1.0 - exp(-dist * dist * fogDensity * fogDensity);
        fade = 1.0 - fog;
    }
    ParticleColor = vec4(aColor.rgb, aColor.a * min(magnification, 1.0) * fade);
    gl_Position = projection * eyePos;
    // Scale point size by distance to camera
    gl_PointSize = max(aSize * sqrt(max(magnification, 1.0)) * (300.0 / -eyePos.z), 1.0);
}
