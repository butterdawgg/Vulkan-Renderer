#version 450

// G-buffer geometry pass: transform vertices to world + clip space and pass
// through the data the fragment shader needs to build the TBN basis.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;  // xyz tangent, w handedness
layout(location = 3) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 proj;
} uCamera;

layout(push_constant) uniform Push
{
    mat4 model;
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
} uPush;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUV;

invariant gl_Position;

void main()
{
    vec4 worldPos = uPush.model * vec4(inPos, 1.0);
    vWorldPos = worldPos.xyz;

    // Normal matrix (inverse-transpose of the upper 3x3) for non-uniform scale.
    mat3 normalMat = transpose(inverse(mat3(uPush.model)));
    vNormal = normalize(normalMat * inNormal);
    vTangent = vec4(normalize(mat3(uPush.model) * inTangent.xyz), inTangent.w);
    vUV = inUV;

    // Same operand order as the depth prepass so the EQUAL test matches exactly.
    gl_Position = uCamera.proj * uCamera.view * uPush.model * vec4(inPos, 1.0);
}
