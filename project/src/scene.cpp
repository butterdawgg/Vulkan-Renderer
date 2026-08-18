#include "scene.h"
#include "mesh.h"
#include "material.h"
#include "gltf_loader.h"
#include "debug_utils.h"

#include <format>
#include <limits>

Scene::Scene() = default;

Scene::~Scene() = default;

void Scene::LoadFromGltf(const std::string& path)
{
    GLTF_Loader::Get().LoadGltfScene(*this, path);
    ComputeBounds();
}

std::vector<std::unique_ptr<Mesh>>& Scene::GetMeshes()
{
    return m_Meshes;
}

std::vector<std::unique_ptr<Material>>& Scene::GetMaterials()
{
    return m_Materials;
}

SceneLighting& Scene::GetLighting()
{
    return m_Lighting;
}

void Scene::SetEnvironmentMap(const std::string& path)
{
    m_EnvironmentMap = path;
}

const std::string& Scene::GetEnvironmentMap() const
{
    return m_EnvironmentMap;
}

void Scene::ComputeBounds()
{
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());

    for (auto& mesh : m_Meshes)
    {
        const glm::mat4 model = mesh->GetTransform().GetModelMatrix();
        for (const Vertex& v : mesh->GetVertices())
        {
            glm::vec3 world = glm::vec3(model * glm::vec4(v.pos, 1.0f));
            mn = glm::min(mn, world);
            mx = glm::max(mx, world);
        }
    }

    if (mn.x > mx.x) // no vertices
    {
        logWarning("Scene::ComputeBounds: no vertices found, keeping default bounds");
        return;
    }

    m_BoundsMin = mn;
    m_BoundsMax = mx;

    logMsg(std::format("Scene AABB: min({:.2f}, {:.2f}, {:.2f}) max({:.2f}, {:.2f}, {:.2f})",
        mn.x, mn.y, mn.z, mx.x, mx.y, mx.z));
}

glm::vec3 Scene::GetBoundsMin() const { return m_BoundsMin; }
glm::vec3 Scene::GetBoundsMax() const { return m_BoundsMax; }
glm::vec3 Scene::GetBoundsCenter() const { return (m_BoundsMin + m_BoundsMax) * 0.5f; }
float Scene::GetBoundsRadius() const { return glm::length(m_BoundsMax - m_BoundsMin) * 0.5f; }