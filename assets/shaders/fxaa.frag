#version 330 core
// FXAA 3.11 (Timothy Lottes, NVIDIA), quality preset 39 in a single pass: finds edges by local luma contrast, searches
// along them in both directions and blends across by the sub-pixel offset. Runs on the tone mapped, display-encoded
// image (perceptual space, where FXAA expects its input), before the upscale and the composite.

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D sourceTexture; // bilinear filtering, clamped
uniform vec2 texel;              // 1 / size

const float EDGE_THRESHOLD = 0.125;      // FXAA_QUALITY__EDGE_THRESHOLD: skip edges with less contrast than this
const float EDGE_THRESHOLD_MIN = 0.0312; // ... and darks where the contrast is below this
const float SUBPIX = 0.75;               // amount of sub-pixel aliasing removal
const int SEARCH_STEPS = 12;
const float STEP[12] = float[12](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 rgbAt(vec2 uv) { return textureLod(sourceTexture, uv, 0.0).rgb; }
float lumaAt(vec2 uv) { return luma(rgbAt(uv)); }
float lumaOff(vec2 uv, vec2 o) { return lumaAt(uv + o * texel); }

void main() {
    vec2 uv = TexCoords;
    vec3 rgbM = rgbAt(uv);
    float lumaM = luma(rgbM);
    float lumaS = lumaOff(uv, vec2(0.0, -1.0));
    float lumaE = lumaOff(uv, vec2(1.0, 0.0));
    float lumaN = lumaOff(uv, vec2(0.0, 1.0));
    float lumaW = lumaOff(uv, vec2(-1.0, 0.0));

    float rangeMax = max(max(lumaN, lumaW), max(lumaE, max(lumaS, lumaM)));
    float rangeMin = min(min(lumaN, lumaW), min(lumaE, min(lumaS, lumaM)));
    float range = rangeMax - rangeMin;
    if (range < max(EDGE_THRESHOLD_MIN, rangeMax * EDGE_THRESHOLD)) {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    float lumaNW = lumaOff(uv, vec2(-1.0, 1.0));
    float lumaSE = lumaOff(uv, vec2(1.0, -1.0));
    float lumaNE = lumaOff(uv, vec2(1.0, 1.0));
    float lumaSW = lumaOff(uv, vec2(-1.0, -1.0));

    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float subpixRcpRange = 1.0 / range;
    float subpixNSWE = lumaNS + lumaWE;
    float edgeHorzA = (-2.0 * lumaM) + lumaNS;
    float edgeVertA = (-2.0 * lumaM) + lumaWE;
    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float edgeHorzB = (-2.0 * lumaE) + lumaNESE;
    float edgeVertB = (-2.0 * lumaN) + lumaNWNE;
    float lumaNWSW = lumaNW + lumaSW;
    float lumaSWSE = lumaSW + lumaSE;
    float edgeHorz = abs(edgeHorzA) * 2.0 + abs(edgeHorzB) + abs((-2.0 * lumaW) + lumaNWSW);
    float edgeVert = abs(edgeVertA) * 2.0 + abs(edgeVertB) + abs((-2.0 * lumaS) + lumaSWSE);
    float subpixNWSWNESE = lumaNWSW + lumaNESE;
    float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;
    bool horzSpan = edgeHorz >= edgeVert;
    float subpixB = (subpixA * (1.0 / 12.0)) - lumaM;

    float lengthSign = horzSpan ? texel.y : texel.x;
    float lumaN1 = horzSpan ? lumaS : lumaW; // the neighbors across the edge
    float lumaP1 = horzSpan ? lumaN : lumaE;
    float gradientN = lumaN1 - lumaM;
    float gradientP = lumaP1 - lumaM;
    float lumaNN = lumaN1 + lumaM;
    float lumaPP = lumaP1 + lumaM;
    bool pairN = abs(gradientN) >= abs(gradientP);
    float gradient = max(abs(gradientN), abs(gradientP));
    if (pairN)
        lengthSign = -lengthSign;
    float subpixC = clamp(abs(subpixB) * subpixRcpRange, 0.0, 1.0);

    vec2 posB = uv;
    vec2 offNP = horzSpan ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    if (horzSpan)
        posB.y += lengthSign * 0.5;
    else
        posB.x += lengthSign * 0.5;

    vec2 posN = posB - offNP * STEP[0];
    vec2 posP = posB + offNP * STEP[0];
    float subpixD = -2.0 * subpixC + 3.0;
    float lumaEndN = lumaAt(posN);
    float subpixE = subpixC * subpixC;
    float lumaEndP = lumaAt(posP);
    if (!pairN)
        lumaNN = lumaPP;
    float gradientScaled = gradient * 0.25;
    float lumaMM = lumaM - lumaNN * 0.5;
    float subpixF = subpixD * subpixE;
    bool lumaMLTZero = lumaMM < 0.0;

    lumaEndN -= lumaNN * 0.5;
    lumaEndP -= lumaNN * 0.5;
    bool doneN = abs(lumaEndN) >= gradientScaled;
    bool doneP = abs(lumaEndP) >= gradientScaled;
    if (!doneN)
        posN -= offNP * STEP[1];
    if (!doneP)
        posP += offNP * STEP[1];

    for (int i = 2; i < SEARCH_STEPS && !(doneN && doneP); ++i) {
        if (!doneN)
            lumaEndN = lumaAt(posN) - lumaNN * 0.5;
        if (!doneP)
            lumaEndP = lumaAt(posP) - lumaNN * 0.5;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN)
            posN -= offNP * STEP[i];
        if (!doneP)
            posP += offNP * STEP[i];
    }

    float dstN = horzSpan ? uv.x - posN.x : uv.y - posN.y;
    float dstP = horzSpan ? posP.x - uv.x : posP.y - uv.y;
    bool directionN = dstN < dstP;
    float lumaEnd = directionN ? lumaEndN : lumaEndP;
    bool goodSpan = (lumaEnd < 0.0) != lumaMLTZero;
    float spanLength = dstP + dstN;
    float dst = directionN ? dstN : dstP;
    float pixelOffset = dst * (-1.0 / spanLength) + 0.5;
    float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
    float subpixH = subpixF * subpixF * SUBPIX;
    float pixelOffsetFinal = max(pixelOffsetGood, subpixH);

    vec2 posM = uv;
    if (horzSpan)
        posM.y += pixelOffsetFinal * lengthSign;
    else
        posM.x += pixelOffsetFinal * lengthSign;
    FragColor = vec4(rgbAt(posM), 1.0);
}
