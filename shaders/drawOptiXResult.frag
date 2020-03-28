﻿#version 400
#extension GL_ARB_explicit_uniform_location : enable // required in version lower 4.3
#extension GL_ARB_shading_language_420pack : enable // required in version lower 4.2

layout(location = 0) uniform int srcFullWidth;
layout(location = 1, binding = 0) uniform samplerBuffer srcTexture;

layout(origin_upper_left) in vec4 gl_FragCoord;

out vec4 color;

void main(void) {
    vec2 srcPixel = gl_FragCoord.xy;
    vec4 opResult = texelFetch(srcTexture, int(srcPixel.y) * srcFullWidth + int(srcPixel.x));
    opResult.rgb = max(opResult.rgb, 0.0f);
    color = opResult;
}
