#ifndef RENDER_SETTINGS_H
#define RENDER_SETTINGS_H

#include <cstdint>

#include <glm/glm.hpp>

struct RenderSettings
{
    // Shadow mapping

    // Directional shadow map resolution in texels (square)
    uint32_t shadowMapResolution { 2048 };

    // Normal-offset depth-compare bias applied when sampling the shadow map
    float shadowBias { 0.0015f };

    // Hardware slope-scaled depth bias applied during the shadow pass
    float shadowDepthBiasConstant { 1.25f };
    float shadowDepthBiasSlope { 1.75f };

    // Ambient / IBL

    // Constant ambient irradiance used when the scene has no IBL environment map
    // Ignored when IBL is active
    glm::vec3 ambientColor { 0.02f, 0.02f, 0.02f };

    // Multiplier on the diffuse IBL ambient contribution
    float iblIntensity { 1.0f };
};

#endif // !RENDER_SETTINGS_H