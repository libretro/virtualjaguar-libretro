#version 450

// Virtual Jaguar first-party shader: sharp bilinear scaling.  Pixels are scaled
// by the whole-number part of the output/source ratio with nearest-neighbour
// crispness, and only the leftover fraction is blended, so a non-integer window
// size does not turn the picture to mush.  Written for this project, licensed
// with the rest of the repository.  RetroArch slang format, single pass.

layout(push_constant) uniform Push {
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 MVP;
} global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main() {
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main() {
    vec2 texel = vTexCoord * params.SourceSize.xy;
    vec2 scale = max(floor(params.OutputSize.xy / params.SourceSize.xy), vec2(1.0));

    vec2 base = floor(texel);
    vec2 centerDist = fract(texel) - 0.5;
    // Inside this band around the texel centre the colour is flat (nearest);
    // outside it the blend ramps over one output pixel.
    vec2 flatRange = 0.5 - 0.5 / scale;
    vec2 f = (centerDist - clamp(centerDist, -flatRange, flatRange)) * scale + 0.5;

    FragColor = vec4(texture(Source, (base + f) / params.SourceSize.xy).rgb, 1.0);
}
