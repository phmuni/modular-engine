// Water: waves that move the surface (vertex), a see-through tint that turns bright and opaque at glancing angles
// (surface). Put it on a flat, finely divided plane: the vertices need to be there to move.
#pragma blend
#pragma noshadow // see-through water casts no shadow; without this line the waves would show in it

uniform vec3 shallowColor = vec3(0.25, 0.75, 0.85);
uniform vec3 deepColor = vec3(0.04, 0.3, 0.5);
uniform float waveHeight = 0.12;
uniform float waveSpeed = 1.0;
uniform float waveScale = 1.5;
uniform float waterAlpha = 0.7;

float waveAt(vec2 p) {
    return sin(p.x * waveScale + time * waveSpeed) * 0.5 +
           sin(p.y * waveScale * 1.3 - time * waveSpeed * 0.8) * 0.5;
}

void vertex(inout VertexData v) {
    const float e = 0.05;
    vec2 p = v.position.xz;
    float h = waveAt(p);
    float hx = waveAt(p + vec2(e, 0.0));
    float hz = waveAt(p + vec2(0.0, e));
    v.position.y += h * waveHeight;
    v.normal = normalize(vec3(-(hx - h) * waveHeight / e, 1.0, -(hz - h) * waveHeight / e));
}

void surface(inout Surface s) {
    float facing = max(dot(normalize(s.normal), s.viewDir), 0.0);
    float edge = pow(1.0 - facing, 3.0);
    s.albedo = mix(deepColor, shallowColor, edge);
    s.emission = shallowColor * edge * 0.35;
    s.alpha = mix(waterAlpha, 1.0, edge);
    s.shininess = 96.0;
    s.specular = vec3(0.9);
}
