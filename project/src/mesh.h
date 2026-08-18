#ifndef MESH_H
#define MESH_H

#include <vector>

#include "transform.h"
#include "vk_types.h"

// Tangent stored as vec4: xyz = tangent, w = handedness (+1/-1) so the
// bitangent can be reconstructed in the shader as cross(N, T) * w
struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 tangent;
    glm::vec2 texCoord;
};

class Mesh
{
    public:

    Mesh();

    Transform& GetTransform();
    std::vector<Vertex>& GetVertices();
    std::vector<uint32_t>& GetIndices();

    size_t GetMaterialId() const;
    void SetMaterialId(size_t id);

    // GPU resources live on the mesh itself (filled in by the renderer on upload)
    AllocatedBuffer& GetVertexBuffer();
    AllocatedBuffer& GetIndexBuffer();

    uint32_t GetIndexCount() const;
    void SetIndexCount(uint32_t count);

    private:

    Transform transform { };

    std::vector<Vertex> vertices { };
    std::vector<uint32_t> indices { };

    size_t materialId { 0 };

    AllocatedBuffer vertexBuffer { };
    AllocatedBuffer indexBuffer { };
    uint32_t indexCount { 0 };
};

#endif // !MESH_H
