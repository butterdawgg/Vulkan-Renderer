#ifndef LIGHTING_H
#define LIGHTING_H

#include <vector>

#include <glm/glm.hpp>

// CPU-side light descriptions (physically based units)

struct DirectionalLight
{
    glm::vec3 direction { -0.4f, -1.0f, -0.3f };
    glm::vec3 color { 1.0f, 1.0f, 1.0f };
    float illuminanceLux { 100000.0f }; // full daylight
};

struct PointLight
{
    glm::vec3 position { 0.0f, 2.0f, 0.0f };
    glm::vec3 color { 1.0f, 1.0f, 1.0f };
    float intensityCandela { 40.0f };
    float range { 25.0f };
};

constexpr uint32_t MAX_POINT_LIGHTS = 16;

// --- GPU-side packed layouts ---

struct GpuDirectionalLight
{
    glm::vec4 direction; // xyz dir, w unused
    glm::vec4 color; // rgb colour, w = illuminance (lux)
};

struct GpuPointLight
{
    glm::vec4 position; // xyz position, w = range
    glm::vec4 color; // rgb colour, w = intensity (candela)
};

// A whole scene's lighting state
class SceneLighting
{
    public:

    DirectionalLight& GetDirectional() { return m_Directional; }
    const DirectionalLight& GetDirectional() const { return m_Directional; }

    std::vector<PointLight>& GetPointLights() { return m_PointLights; }
    const std::vector<PointLight>& GetPointLights() const { return m_PointLights; }

    private:

    DirectionalLight m_Directional { };
    std::vector<PointLight> m_PointLights { };
};

#endif // !LIGHTING_H