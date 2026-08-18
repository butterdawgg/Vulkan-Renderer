#include "mesh.h"



Mesh::Mesh()
{ }

Transform& Mesh::GetTransform()
{
    return transform;
}

std::vector<Vertex>& Mesh::GetVertices()
{
    return vertices;
}

std::vector<uint32_t>& Mesh::GetIndices()
{
    return indices;
}

size_t Mesh::GetMaterialId() const
{
    return materialId;
}

void Mesh::SetMaterialId(size_t id)
{
    materialId = id;
}

AllocatedBuffer& Mesh::GetVertexBuffer()
{
    return vertexBuffer;
}

AllocatedBuffer& Mesh::GetIndexBuffer()
{
    return indexBuffer;
}

uint32_t Mesh::GetIndexCount() const
{
    return indexCount;
}

void Mesh::SetIndexCount(uint32_t count)
{
    indexCount = count;
}
