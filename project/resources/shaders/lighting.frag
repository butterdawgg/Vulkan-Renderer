#version 450

// Deferred lighting pass. Reads the G-buffer, reconstructs world position from
// depth, and evaluates a Cook-Torrance BRDF for a directional light (with
// shadow mapping) and a set of point lights, plus a diffuse IBL ambient term.
// Output is linear HDR colour - tonemapping happens later in post.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D gAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gMetalRough;
layout(set = 0, binding = 3) uniform sampler2D gEmissive;
layout(set = 0, binding = 4) uniform sampler2D gDepth;
layout(set = 0, binding = 5) uniform sampler2D uShadowMap;
layout(set = 0, binding = 6) uniform samplerCube uIrradiance;

const int MAX_POINT_LIGHTS = 16;

struct PointLight
{
    vec4 position; // xyz pos, w = range
    vec4 color;    // rgb colour, w = intensity (candela)
};

layout(set = 0, binding = 7) uniform LightingUBO
{
    mat4 invViewProj;
    mat4 lightViewProj;
    vec4 camPos;       // xyz
    vec4 dirDirection; // xyz light travel direction
    vec4 dirColor;     // rgb colour, w = illuminance (lux)
    vec4 ambient;      // rgb constant fallback, w = hasIBL (>0.5)
    vec4 params;       // x = numPointLights, y = shadowBias, z = iblIntensity, w unused
    PointLight points[MAX_POINT_LIGHTS];
} uLight;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 CookTorrance(vec3 N, vec3 V, vec3 L, vec3 radiance,
    vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 1e-4;
    vec3 specular = numerator / denom;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

float SampleShadow(vec3 worldPos, vec3 N, vec3 L)
{
    vec4 lc = uLight.lightViewProj * vec4(worldPos, 1.0);
    vec3 proj = lc.xyz / lc.w;

    // Outside the light frustum -> fully lit.
    if (proj.z > 1.0 || proj.z < 0.0) return 1.0;

    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;

    float bias = max(uLight.params.y * (1.0 - dot(N, L)), uLight.params.y * 0.1);

    // 3x3 PCF
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float closest = texture(uShadowMap, uv + vec2(x, y) * texel).r;
            shadow += (proj.z - bias > closest) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

void main()
{
    float depth = texture(gDepth, vUV).r;

    // No geometry (cleared far depth): leave background black.
    if (depth >= 1.0)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Reconstruct world position from depth.
    vec4 clip = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 world = uLight.invViewProj * clip;
    vec3 worldPos = world.xyz / world.w;

    vec3 albedo   = texture(gAlbedo, vUV).rgb;
    vec3 N        = normalize(texture(gNormal, vUV).rgb * 2.0 - 1.0);
    vec3 mr       = texture(gMetalRough, vUV).rgb;
    float metallic  = mr.r;
    float roughness = mr.g;
    float ao        = mr.b;
    vec3 emissive = texture(gEmissive, vUV).rgb;

    vec3 V = normalize(uLight.camPos.xyz - worldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // --- Directional light (with shadow) ---
    {
        vec3 L = normalize(-uLight.dirDirection.xyz);
        vec3 radiance = uLight.dirColor.rgb * uLight.dirColor.w; // lux
        float shadow = SampleShadow(worldPos, N, L);
        Lo += CookTorrance(N, V, L, radiance, albedo, metallic, roughness, F0) * shadow;
    }

    // --- Point lights ---
    int numPoints = int(uLight.params.x);
    for (int i = 0; i < numPoints && i < MAX_POINT_LIGHTS; ++i)
    {
        vec3 lightPos = uLight.points[i].position.xyz;
        float range   = uLight.points[i].position.w;
        vec3 lightCol = uLight.points[i].color.rgb;
        float intensity = uLight.points[i].color.w; // candela

        vec3 toLight = lightPos - worldPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-4);

        float att = 1.0 / max(dist * dist, 1e-4);
        float window = clamp(1.0 - pow(dist / max(range, 1e-4), 4.0), 0.0, 1.0);
        att *= window * window;

        vec3 radiance = lightCol * intensity * att;
        Lo += CookTorrance(N, V, L, radiance, albedo, metallic, roughness, F0);
    }

    // --- Diffuse IBL ambient ---
    vec3 irradiance = (uLight.ambient.w > 0.5)
        ? texture(uIrradiance, N).rgb
        : uLight.ambient.rgb;

    vec3 kD = (1.0 - F0) * (1.0 - metallic);
    vec3 ambient = kD * irradiance * albedo * ao * uLight.params.z;

    vec3 color = Lo + ambient + emissive;
    outColor = vec4(color, 1.0);
}
