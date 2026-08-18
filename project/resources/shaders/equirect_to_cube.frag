#version 450

// Projects an equirectangular HDR image onto a cubemap face. Sampled per cube
// direction; the result is rendered into one layer of the environment cubemap.

layout(location = 0) in vec3 vLocalPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uEquirect;

const vec2 invAtan = vec2(0.1591, 0.3183); // 1/(2pi), 1/pi

vec2 SampleSphericalMap(vec3 dir)
{
    vec2 uv = vec2(atan(dir.z, dir.x), asin(dir.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main()
{
    vec3 dir = normalize(vLocalPos);
    vec2 uv = SampleSphericalMap(dir);
    outColor = vec4(texture(uEquirect, uv).rgb, 1.0);
}
