#version 330 core
// AMD FidelityFX Super Resolution 1.0, EASU (Edge Adaptive Spatial Upsampling): a GLSL 330 fragment-shader port of
// FsrEasuF from ffx_fsr1.h (no compute, no gather: the 12 taps are read with texelFetch).
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Input: the tone mapped, display-encoded (perceptual) image at the render size. Output: the screen's size.

out vec4 FragColor;

uniform sampler2D sourceTexture;
uniform vec2 inputSize;  // render size in pixels
uniform vec2 outputSize; // screen size in pixels

vec3 fetchTap(ivec2 p) {
    ivec2 limit = ivec2(inputSize) - 1;
    return texelFetch(sourceTexture, clamp(p, ivec2(0), limit), 0).rgb;
}

float lumaOf(vec3 c) { return c.g + 0.5 * (c.r + c.b); } // luma times 2, as in the reference

// Accumulates the edge direction and length of one of the four bilinear-weighted 'plus' neighborhoods.
//    a
//  b c d
//    e
void easuSet(inout vec2 dir, inout float len, vec2 pp, bool biS, bool biT, bool biU, bool biV, float lA, float lB,
             float lC, float lD, float lE) {
    float w = 0.0;
    if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
    if (biT) w = pp.x * (1.0 - pp.y);
    if (biU) w = (1.0 - pp.x) * pp.y;
    if (biV) w = pp.x * pp.y;

    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = lenX > 0.0 ? 1.0 / lenX : 0.0;
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;

    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = lenY > 0.0 ? 1.0 / lenY : 0.0;
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

// One tap of the kernel: a lanczos2 approximation (no sin, no sqrt) over the anisotropic, rotated distance
void easuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v;
    v.x = (off.x * dir.x) + (off.y * dir.y);
    v.y = (off.x * (-dir.y)) + (off.y * dir.x);
    v *= len;
    float d2 = v.x * v.x + v.y * v.y;
    d2 = min(d2, clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

void main() {
    // Position of 'f' in the input: the output pixel's center mapped into it
    vec2 ip = floor(gl_FragCoord.xy);
    vec2 scale = inputSize / outputSize;
    vec2 pp = ip * scale + (0.5 * scale - 0.5);
    vec2 fp = floor(pp);
    pp -= fp;
    ivec2 f0 = ivec2(fp);

    // 12-tap kernel, y down
    //    b c
    //  e f g h
    //  i j k l
    //    n o
    vec3 bC = fetchTap(f0 + ivec2(0, -1));
    vec3 cC = fetchTap(f0 + ivec2(1, -1));
    vec3 eC = fetchTap(f0 + ivec2(-1, 0));
    vec3 fC = fetchTap(f0 + ivec2(0, 0));
    vec3 gC = fetchTap(f0 + ivec2(1, 0));
    vec3 hC = fetchTap(f0 + ivec2(2, 0));
    vec3 iC = fetchTap(f0 + ivec2(-1, 1));
    vec3 jC = fetchTap(f0 + ivec2(0, 1));
    vec3 kC = fetchTap(f0 + ivec2(1, 1));
    vec3 lC = fetchTap(f0 + ivec2(2, 1));
    vec3 nC = fetchTap(f0 + ivec2(0, 2));
    vec3 oC = fetchTap(f0 + ivec2(1, 2));
    float bL = lumaOf(bC), cL = lumaOf(cC), eL = lumaOf(eC), fL = lumaOf(fC), gL = lumaOf(gC), hL = lumaOf(hC);
    float iL = lumaOf(iC), jL = lumaOf(jC), kL = lumaOf(kC), lL = lumaOf(lC), nL = lumaOf(nC), oL = lumaOf(oC);

    // Direction and length of the local edge, from the four center texels' neighborhoods
    vec2 dir = vec2(0.0);
    float len = 0.0;
    easuSet(dir, len, pp, true, false, false, false, bL, eL, fL, gL, jL);
    easuSet(dir, len, pp, false, true, false, false, cL, fL, gL, hL, kL);
    easuSet(dir, len, pp, false, false, true, false, fL, iL, jL, kL, nL);
    easuSet(dir, len, pp, false, false, false, true, gL, jL, kL, lL, oL);

    // Normalize the direction, cleaning up close to zero
    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < (1.0 / 32768.0);
    dirR = inversesqrt(max(dirR, 1e-12));
    dirR = zro ? 1.0 : dirR;
    dir.x = zro ? 1.0 : dir.x;
    dir *= vec2(dirR);

    // {0 to 2} -> {0 to 1}, shaped with a square
    len = len * 0.5;
    len *= len;
    // Stretch the kernel: 1 along an axis, up to sqrt(2) on a diagonal
    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
    // Anisotropic length after rotation: x from 1 to 'stretch' on edges, y from 1 to 2x on edges
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    // The window shifts from +/-sqrt(2) to slightly beyond 2 by the amount of edge
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    // Clip distance^2 at the end of the adjustable window
    float clp = 1.0 / lob;

    // The nearest four bound the result (deringing)
    vec3 min4 = min(min(fC, gC), min(jC, kC));
    vec3 max4 = max(max(fC, gC), max(jC, kC));

    vec3 aC = vec3(0.0);
    float aW = 0.0;
    easuTap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, bC);
    easuTap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, cC);
    easuTap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, iC);
    easuTap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, jC);
    easuTap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, fC);
    easuTap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, eC);
    easuTap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, kC);
    easuTap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, lC);
    easuTap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, hC);
    easuTap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, gC);
    easuTap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, oC);
    easuTap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, nC);

    vec3 pix = min(max4, max(min4, aC / aW));
    FragColor = vec4(clamp(pix, 0.0, 1.0), 1.0);
}
