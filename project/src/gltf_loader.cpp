#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <filesystem>
namespace fs = std::filesystem;

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <functional>

#include "gltf_loader.h"
#include "scene.h"
#include "mesh.h"
#include "material.h"
#include "debug_utils.h"

class GLTF_Loader::Impl
{
    public:

    Impl() { }
    ~Impl() { }

    void LoadGltfScene(Scene& scene, const std::string& path)
    {
        auto& meshes { scene.GetMeshes() };
        auto& materials { scene.GetMaterials() };

        tinygltf::Model model;
        std::string err, warn;

        const bool isBinary = path.size() >= 4 &&
            path.substr(path.size() - 4) == ".glb";

        bool ok = isBinary
            ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
            : loader.LoadASCIIFromFile(&model, &err, &warn, path);

        if (!warn.empty()) logWarning(warn);
        if (!err.empty())  logError(err);
        if (!ok)           throw std::runtime_error("TinyGLTF failed to load '" + path + "': " + err);
        if (model.meshes.empty())
            throw std::runtime_error("glTF file contains no meshes: " + path);

        // --- Load materials (full metallic-roughness PBR set) ---
        for (auto& gltfMat : model.materials)
        {
            materials.push_back(std::make_unique<Material>(
                LoadGltfMaterial(model, gltfMat, path)));
        }

        if (materials.empty())
            materials.push_back(std::make_unique<Material>());

        // --- Helpers ---
        auto getNodeTransform = [](const tinygltf::Node& node) -> glm::mat4
        {
            if (node.matrix.size() == 16)
                return glm::make_mat4(node.matrix.data());

            glm::vec3 translation(0.0f);
            glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
            glm::vec3 scale(1.0f);

            if (node.translation.size() == 3)
                translation = glm::vec3(
                    static_cast<float>(node.translation[0]),
                    static_cast<float>(node.translation[1]),
                    static_cast<float>(node.translation[2]));

            if (node.rotation.size() == 4)
                rotation = glm::quat(
                    static_cast<float>(node.rotation[3]),
                    static_cast<float>(node.rotation[0]),
                    static_cast<float>(node.rotation[1]),
                    static_cast<float>(node.rotation[2]));

            if (node.scale.size() == 3)
                scale = glm::vec3(
                    static_cast<float>(node.scale[0]),
                    static_cast<float>(node.scale[1]),
                    static_cast<float>(node.scale[2]));

            return
                glm::translate(glm::mat4(1.0f), translation) *
                glm::mat4_cast(rotation) *
                glm::scale(glm::mat4(1.0f), scale);
        };

        auto getBufferPtr = [&model](const tinygltf::Accessor& accessor) -> const uint8_t*
        {
            const auto& view = model.bufferViews[accessor.bufferView];
            const auto& buffer = model.buffers[view.buffer];
            return buffer.data.data() + view.byteOffset + accessor.byteOffset;
        };

        // --- Recursive node traversal ---
        std::function<void(int, const glm::mat4&)> processNode;
        processNode = [&](int nodeIndex, const glm::mat4& parentTransform)
        {
            const tinygltf::Node& node = model.nodes[nodeIndex];
            glm::mat4 worldTransform = parentTransform * getNodeTransform(node);

            if (node.mesh >= 0)
            {
                const tinygltf::Mesh& gltfMesh = model.meshes[node.mesh];

                for (const auto& primitive : gltfMesh.primitives)
                {
                    if (primitive.attributes.find("POSITION") == primitive.attributes.end())
                        continue;

                    auto mesh = std::make_unique<Mesh>();

                    glm::vec3 translation, scale, skew;
                    glm::quat rotation;
                    glm::vec4 perspective;
                    glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);

                    auto& meshTransform { mesh->GetTransform() };
                    meshTransform.SetPosition(translation);
                    meshTransform.SetScale(scale);
                    meshTransform.SetRotation(glm::eulerAngles(rotation));

                    mesh->SetMaterialId(
                        primitive.material >= 0
                        ? static_cast<size_t>(primitive.material)
                        : 0);

                    const auto& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
                    const float* positions = reinterpret_cast<const float*>(getBufferPtr(posAccessor));

                    const float* normals = nullptr;
                    if (auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end())
                        normals = reinterpret_cast<const float*>(getBufferPtr(model.accessors[it->second]));

                    const float* tangents = nullptr;
                    if (auto it = primitive.attributes.find("TANGENT"); it != primitive.attributes.end())
                        tangents = reinterpret_cast<const float*>(getBufferPtr(model.accessors[it->second]));

                    const float* texcoords = nullptr;
                    if (auto it = primitive.attributes.find("TEXCOORD_0"); it != primitive.attributes.end())
                        texcoords = reinterpret_cast<const float*>(getBufferPtr(model.accessors[it->second]));

                    size_t vertexCount = posAccessor.count;

                    auto& meshVertices { mesh->GetVertices() };
                    meshVertices.reserve(vertexCount);

                    for (size_t i = 0; i < vertexCount; i++)
                    {
                        Vertex v { };
                        v.pos = glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);

                        if (normals)
                            v.normal = glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);

                        if (tangents)
                            v.tangent = glm::vec4(tangents[i * 4 + 0], tangents[i * 4 + 1],
                                tangents[i * 4 + 2], tangents[i * 4 + 3]);
                        else
                            v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

                        if (texcoords)
                            v.texCoord = glm::vec2(texcoords[i * 2 + 0], texcoords[i * 2 + 1]);

                        meshVertices.push_back(v);
                    }

                    // Indices
                    auto& meshIndices { mesh->GetIndices() };
                    if (primitive.indices >= 0)
                    {
                        const auto& indexAccessor = model.accessors[primitive.indices];
                        const uint8_t* indexData = getBufferPtr(indexAccessor);
                        meshIndices.reserve(indexAccessor.count);

                        for (size_t i = 0; i < indexAccessor.count; i++)
                        {
                            uint32_t idx = 0;
                            switch (indexAccessor.componentType)
                            {
                                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                                    idx = reinterpret_cast<const uint8_t*>(indexData)[i];  break;
                                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                                    idx = reinterpret_cast<const uint16_t*>(indexData)[i]; break;
                                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                                    idx = reinterpret_cast<const uint32_t*>(indexData)[i]; break;
                                default:
                                    throw std::runtime_error("Unsupported glTF index type");
                            }
                            meshIndices.push_back(idx);
                        }
                    }

                    // If the asset had no tangents, derive them from UVs so that
                    // normal mapping works. (glTF requires UVs for this.)
                    if (!tangents && texcoords && !meshIndices.empty())
                        ComputeTangents(meshVertices, meshIndices);

                    meshes.push_back(std::move(mesh));
                }
            }

            for (int child : node.children)
                processNode(child, worldTransform);
        };

        int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (sceneIndex >= static_cast<int>(model.scenes.size()))
            throw std::runtime_error("Invalid glTF scene");

        const tinygltf::Scene& gltfScene = model.scenes[sceneIndex];
        for (int nodeIndex : gltfScene.nodes)
            processNode(nodeIndex, glm::mat4(1.0f));
    }

    private:

    tinygltf::TinyGLTF loader;

    // Loads a single texture referenced by a glTF texture index into a Texture
    // Returns false if the index is invalid (caller keeps the material default)
    bool LoadTextureFromIndex(const tinygltf::Model& model, int texIdx, bool srgb,
        const std::string& gltfPath, Texture& outTexture)
    {
        if (texIdx < 0) return false;

        const auto& tex = model.textures[texIdx];
        if (tex.source < 0) return false;

        const auto& img = model.images[tex.source];

        if (!img.image.empty())
        {
            outTexture = Texture { img.image,
                static_cast<uint32_t>(img.width),
                static_cast<uint32_t>(img.height) };
        }
        else if (!img.uri.empty())
        {
            fs::path full = fs::path(gltfPath).parent_path() / img.uri;

            int width, height, channels;
            stbi_uc* pixels = stbi_load(full.string().c_str(), &width, &height,
                &channels, STBI_rgb_alpha);

            if (!pixels)
                throw std::runtime_error("Failed to load texture: " + full.string());

            outTexture = Texture { pixels,
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height) };

            stbi_image_free(pixels);
        }
        else
        {
            return false;
        }

        outTexture.SetSrgb(srgb);
        return true;
    }

    Material LoadGltfMaterial(const tinygltf::Model& model,
        const tinygltf::Material& gltfMat, const std::string& gltfPath)
    {
        Material mat { };
        const auto& pbr = gltfMat.pbrMetallicRoughness;

        // Textures (sRGB for colour data, linear for data textures)
        LoadTextureFromIndex(model, pbr.baseColorTexture.index, true,  gltfPath,
            mat.GetTexture(TextureSlot::BaseColor));
        LoadTextureFromIndex(model, pbr.metallicRoughnessTexture.index, false, gltfPath,
            mat.GetTexture(TextureSlot::MetallicRoughness));
        LoadTextureFromIndex(model, gltfMat.normalTexture.index, false, gltfPath,
            mat.GetTexture(TextureSlot::Normal));
        LoadTextureFromIndex(model, gltfMat.emissiveTexture.index, true, gltfPath,
            mat.GetTexture(TextureSlot::Emissive));
        LoadTextureFromIndex(model, gltfMat.occlusionTexture.index, false, gltfPath,
            mat.GetTexture(TextureSlot::Occlusion));

        // Factors
        auto& f = mat.GetFactors();
        if (pbr.baseColorFactor.size() == 4)
            f.baseColorFactor = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3]));

        f.metallicFactor = static_cast<float>(pbr.metallicFactor);
        f.roughnessFactor = static_cast<float>(pbr.roughnessFactor);

        if (gltfMat.emissiveFactor.size() == 3)
            f.emissiveFactor = glm::vec4(
                static_cast<float>(gltfMat.emissiveFactor[0]),
                static_cast<float>(gltfMat.emissiveFactor[1]),
                static_cast<float>(gltfMat.emissiveFactor[2]), 1.0f);

        f.normalScale = static_cast<float>(gltfMat.normalTexture.scale);
        f.occlusionStrength = static_cast<float>(gltfMat.occlusionTexture.strength);

        return mat;
    }

    // Lengyel's method: derive per-vertex tangents (xyz) + handedness (w) from
    // positions and UVs when the asset doesn't provide them
    static void ComputeTangents(std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices)
    {
        std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i + 0];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            const glm::vec3& p0 = vertices[i0].pos;
            const glm::vec3& p1 = vertices[i1].pos;
            const glm::vec3& p2 = vertices[i2].pos;

            const glm::vec2& w0 = vertices[i0].texCoord;
            const glm::vec2& w1 = vertices[i1].texCoord;
            const glm::vec2& w2 = vertices[i2].texCoord;

            glm::vec3 e1 = p1 - p0;
            glm::vec3 e2 = p2 - p0;
            glm::vec2 duv1 = w1 - w0;
            glm::vec2 duv2 = w2 - w0;

            float denom = duv1.x * duv2.y - duv2.x * duv1.y;
            float r = (std::abs(denom) < 1e-8f) ? 0.0f : 1.0f / denom;

            glm::vec3 sdir = (e1 * duv2.y - e2 * duv1.y) * r;
            glm::vec3 tdir = (e2 * duv1.x - e1 * duv2.x) * r;

            tan1[i0] += sdir; tan1[i1] += sdir; tan1[i2] += sdir;
            tan2[i0] += tdir; tan2[i1] += tdir; tan2[i2] += tdir;
        }

        for (size_t i = 0; i < vertices.size(); i++)
        {
            glm::vec3 n = vertices[i].normal;
            glm::vec3 t = tan1[i];

            if (glm::length(t) < 1e-8f)
                t = glm::vec3(1.0f, 0.0f, 0.0f);

            // Gram-Schmidt orthonormalise.
            glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));
            float handedness = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;

            vertices[i].tangent = glm::vec4(tangent, handedness);
        }
    }
};

GLTF_Loader::GLTF_Loader()
{
    m_Impl = std::make_unique<GLTF_Loader::Impl>();
}

GLTF_Loader::~GLTF_Loader() = default;

void GLTF_Loader::LoadGltfScene(Scene& scene, const std::string& path)
{
    m_Impl->LoadGltfScene(scene, path);
}
