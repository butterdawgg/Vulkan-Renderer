#version 450

// Post-processing: applies the physical-camera exposure multiplier and the ACES
// filmic tonemap operator. This is the single point where HDR scene colour
// collapses to displayable LDR.
//
// The post pass renders into the swapchain, which is created with an _SRGB
// format, so the hardware performs the linear->sRGB encode on store. We
// therefore output LINEAR colour here and do NOT apply a gamma curve manually
// (doing both would double-correct).

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uHDR;

layout(push_constant) uniform Push
{
    float exposure; // linear multiplier derived from camera EV100
} uPush;

// ACES filmic tonemap (Narkowicz fit). Input/output treated as linear.
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(uHDR, vUV).rgb;
    hdr *= uPush.exposure;             // physical exposure
    vec3 ldr = ACESFilm(hdr);          // tonemap HDR -> [0,1] linear
    outColor = vec4(ldr, 1.0);         // sRGB encode done by the swapchain
}
