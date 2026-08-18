#version 450

// Renders scene depth from the directional light's point of view into the
// shadow map. Depth-only (no fragment shader).
layout(location = 0) in vec3 inPos;

layout(push_constant) uniform Push
{
    mat4 lightViewProj; // light-space view-projection
    mat4 model;
} uPush;

void main()
{
    gl_Position = uPush.lightViewProj * uPush.model * vec4(inPos, 1.0);
}
