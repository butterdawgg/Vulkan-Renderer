#ifndef SCENE_H
#define SCENE_H

#include <vector>
#include <string>
#include <memory>

#include <glm/glm.hpp>

#include "lighting.h"

class Mesh;
class Material;

class Scene
{
    public:

    Scene();
    ~Scene();

    void LoadFromGltf(const std::string& path);

    std::vector<std::unique_ptr<Mesh>>& GetMeshes();
    std::vector<std::unique_ptr<Material>>& GetMaterials();

    SceneLighting& GetLighting();

    // Path to an equirectangular .hdr used for image-based lighting
    // If empty, the renderer falls back to a constant ambient irradiance
    void SetEnvironmentMap(const std::string& path);
    const std::string& GetEnvironmentMap() const;

    // World-space bounds, computed after loading
    void ComputeBounds();
    glm::vec3 GetBoundsMin() const;
    glm::vec3 GetBoundsMax() const;
    glm::vec3 GetBoundsCenter() const;
    float GetBoundsRadius() const;

    private:

    std::vector<std::unique_ptr<Mesh>> m_Meshes;
    std::vector<std::unique_ptr<Material>> m_Materials;

    SceneLighting m_Lighting { };
    std::string m_EnvironmentMap { };

    glm::vec3 m_BoundsMin { -1.0f, -1.0f, -1.0f };
    glm::vec3 m_BoundsMax { 1.0f,  1.0f,  1.0f };
};

#endif // !SCENE_H