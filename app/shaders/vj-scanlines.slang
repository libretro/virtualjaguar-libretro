#version 450

// Virtual Jaguar first-party shader: simple scanlines with an optional RGB
// triad mask.  Written for this project (not derived from any other shader),
// licensed with the rest of the repository.  RetroArch slang format, single pass.

layout(push_constant) uniform Push {
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
    float SCAN_STRENGTH;
    float MASK_STRENGTH;
    float BRIGHTNESS;
} params;

#pragma parameter SCAN_STRENGTH "Scanline strength" 0.45 0.0 1.0 0.05
#pragma parameter MASK_STRENGTH "RGB mask strength" 0.0 0.0 1.0 0.05
#pragma parameter BRIGHTNESS "Brightness compensation" 1.15 0.5 2.0 0.05

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
    vec3 c = texture(Source, vTexCoord).rgb;

    // One bright line per source line: the beam is brightest in the middle of a
    // source line and fades towards its edges.
    float d = fract(vTexCoord.y * params.SourceSize.y) - 0.5;   // -0.5 .. 0.5
    float beam = cos(d * 3.14159265);
    float scan = mix(1.0, beam * beam, params.SCAN_STRENGTH);

    // Optional triad: every third output column keeps one channel at full strength.
    vec3 mask = vec3(1.0 - params.MASK_STRENGTH);
    mask[int(mod(gl_FragCoord.x, 3.0))] = 1.0;

    FragColor = vec4(c * scan * mask * params.BRIGHTNESS, 1.0);
}
