#version 450

// Fills the G-buffer: albedo, world-space normal (after normal mapping),
// metallic/roughness/AO and emissive. No lighting here - that is deferred.
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUV;

layout(set = 1, binding = 0) uniform sampler2D uBaseColor;
layout(set = 1, binding = 1) uniform sampler2D uMetallicRoughness;
layout(set = 1, binding = 2) uniform sampler2D uNormal;
layout(set = 1, binding = 3) uniform sampler2D uEmissive;
layout(set = 1, binding = 4) uniform sampler2D uOcclusion;

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

layout(location = 0) out vec4 outAlbedo;     // rgb albedo
layout(location = 1) out vec4 outNormal;     // rgb world normal
layout(location = 2) out vec4 outMetalRough; // r=metallic g=roughness b=ao
layout(location = 3) out vec4 outEmissive;   // rgb emissive

void main()
{
    vec4 baseColor = texture(uBaseColor, vUV) * uPush.baseColorFactor;

    // glTF packs roughness in G, metallic in B of the MR texture.
    vec3 mr = texture(uMetallicRoughness, vUV).rgb;
    float metallic  = mr.b * uPush.metallicFactor;
    float roughness = clamp(mr.g * uPush.roughnessFactor, 0.04, 1.0);

    float ao = texture(uOcclusion, vUV).r;
    ao = mix(1.0, ao, uPush.occlusionStrength);

    // Tangent-space normal mapping.
    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
    vec3 B = cross(N, T) * vTangent.w;
    mat3 TBN = mat3(T, B, N);

    vec3 sampledN = texture(uNormal, vUV).rgb * 2.0 - 1.0;
    sampledN.xy *= uPush.normalScale;
    vec3 worldN = normalize(TBN * sampledN);

    vec3 emissive = texture(uEmissive, vUV).rgb * uPush.emissiveFactor.rgb;

    outAlbedo     = vec4(baseColor.rgb, 1.0);
    outNormal     = vec4(worldN * 0.5 + 0.5, 1.0); // store encoded [0,1]
    outMetalRough = vec4(metallic, roughness, ao, 1.0);
    outEmissive   = vec4(emissive, 1.0);
}
