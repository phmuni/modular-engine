#version 330 core
// AMD FidelityFX Super Resolution 1.0, RCAS (Robust Contrast Adaptive Sharpening): a GLSL 330 fragment-shader port of
// FsrRcasF from ffx_fsr1.h.
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
// Input: EASU's output at the screen's size. The reference's optional noise removal (FSR_RCAS_DENOISE) is left out.

out vec4 FragColor;

uniform sampler2D sourceTexture;
uniform float sharpness; // in stops of attenuation: 0 is the sharpest, 2 the mildest (AMD's default: 0.2)

const float FSR_RCAS_LIMIT = 0.25 - (1.0 / 16.0);

vec3 load(ivec2 p) {
    return texelFetch(sourceTexture, clamp(p, ivec2(0), textureSize(sourceTexture, 0) - 1), 0).rgb;
}

void main() {
    ivec2 sp = ivec2(gl_FragCoord.xy);
    //    b
    //  d e f
    //    h
    vec3 b = load(sp + ivec2(0, -1));
    vec3 d = load(sp + ivec2(-1, 0));
    vec3 e = load(sp);
    vec3 f = load(sp + ivec2(1, 0));
    vec3 h = load(sp + ivec2(0, 1));

    // Min and max of the ring
    vec3 mn4 = min(min(b, d), min(f, h));
    vec3 mx4 = max(max(b, d), max(f, h));

    // Limiters: how much sharpening each channel can take before clipping at 0 or 1
    vec2 peakC = vec2(1.0, -4.0);
    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1e-5));
    vec3 hitMax = (peakC.x - max(mx4, e)) / (4.0 * mn4 + peakC.y);
    vec3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * exp2(-sharpness);

    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    vec3 pix = (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;
    FragColor = vec4(clamp(pix, 0.0, 1.0), 1.0);
}
