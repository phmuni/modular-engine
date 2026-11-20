// Hologram: a glowing, flickering see-through look with scan lines that move up the model.
#pragma blend

uniform vec3 holoColor = vec3(0.2, 0.9, 1.0);
uniform float scanSpeed = 0.6;
uniform float lineCount = 30.0;
uniform float flicker = 0.15;

void surface(inout Surface s) {
    float scan = 0.5 + 0.5 * sin(s.worldPos.y * lineCount - time * scanSpeed * 10.0);
    float rim = pow(1.0 - max(dot(normalize(s.normal), s.viewDir), 0.0), 2.0);
    float blink = 1.0 - flicker * step(0.9, fract(sin(floor(time * 24.0) * 12.9898) * 43758.5453));

    s.albedo = holoColor * 0.15;
    s.emission = holoColor * (0.35 + 0.65 * scan + rim * 1.5) * blink;
    s.alpha = clamp(0.2 + 0.4 * scan + rim, 0.0, 1.0) * blink;
}
