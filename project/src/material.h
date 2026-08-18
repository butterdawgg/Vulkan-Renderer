#ifndef MATERIAL_H
#define MATERIAL_H

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "vk_types.h"

// A CPU-side texture: raw RGBA8 pixels plus dimensions; defaults to a single
// white texel so a material always has something valid to bind
class Texture
{
    public:

    Texture();
    Texture(uint8_t* pixels, uint32_t width, uint32_t height);
    Texture(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height);

    // 1x1 texture of a constant RGBA colour (used for material defaults)
    static Texture Solid(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    std::vector<uint8_t>& GetPixels();
    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

    // sRGB images (base colour, emissive) are uploaded with an sRGB format so
    // the hardware linearises on sample; data textures (normal, metal/rough,
    // occlusion) stay linear (UNORM)
    bool IsSrgb() const;
    void SetSrgb(bool srgb);

    private:

    std::vector<uint8_t> m_Pixels { 255, 255, 255, 255 };
    uint32_t m_Width { 1 };
    uint32_t m_Height { 1 };
    bool m_Srgb { false };
};

// The set of textures a metallic-roughness PBR material can carry; indices are
// used both for the CPU Texture array and the uploaded GPU image array
enum class TextureSlot : uint32_t
{
    BaseColor = 0,
    MetallicRoughness = 1,
    Normal = 2,
    Emissive = 3,
    Occlusion = 4,
    Count = 5
};

// Scalar/vector factors, laid out to match the push-constant block the
// G-buffer shader expects (see gbuffer.frag)
struct MaterialFactors
{
    glm::vec4 baseColorFactor { 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 emissiveFactor { 0.0f, 0.0f, 0.0f, 1.0f }; // .w unused (padding)
    float metallicFactor { 1.0f };
    float roughnessFactor { 1.0f };
    float normalScale { 1.0f };
    float occlusionStrength { 1.0f };
};

class Material
{
    public:

    Material();

    Texture& GetTexture(TextureSlot slot);
    AllocatedImage& GetImage(TextureSlot slot);

    MaterialFactors& GetFactors();
    const MaterialFactors& GetFactors() const;

    private:

    std::array<Texture, static_cast<size_t>(TextureSlot::Count)> m_Textures { };
    std::array<AllocatedImage, static_cast<size_t>(TextureSlot::Count)> m_Images { };

    MaterialFactors m_Factors { };
};

#endif // !MATERIAL_H
