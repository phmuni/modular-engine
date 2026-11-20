// Dissolve: a noise threshold eats the model away, and a glowing edge burns along the cut. Animate `dissolve` from 0
// (whole) to 1 (gone) from a script: entity:setShaderParam("dissolve", amount).

uniform float dissolve = 0.4;
uniform float noiseScale = 5.0;
uniform float edgeWidth = 0.08;
uniform vec3 edgeColor = vec3(1.0, 0.45, 0.1);

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
                   mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
                   mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

void surface(inout Surface s) {
    float n = noise(s.worldPos * noiseScale);
    cutBelow(n, dissolve); // a discard, or a smooth edge when antialiasing is on
    float edge = 1.0 - smoothstep(dissolve, dissolve + edgeWidth, n);
    s.emission += edgeColor * edge * 3.0;
}
