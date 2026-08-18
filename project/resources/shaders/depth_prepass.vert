#version 450

// Depth prepass: positions only. Writes depth that the G-buffer pass reuses
// with an EQUAL test so each covered pixel is shaded exactly once.
layout(location = 0) in vec3 inPos;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 proj;
} uCamera;

layout(push_constant) uniform Push
{
    mat4 model;
} uPush;

invariant gl_Position;

void main()
{
    gl_Position = uCamera.proj * uCamera.view * uPush.model * vec4(inPos, 1.0);
}
