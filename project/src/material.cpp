#include "material.h"

Texture::Texture()
{ }

Texture::Texture(uint8_t* pixels, uint32_t width, uint32_t height) :
    m_Pixels(std::vector<uint8_t>(pixels, pixels + static_cast<size_t>(width) * height * 4)),
    m_Width(width), m_Height(height)
{ }

Texture::Texture(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height) :
    m_Pixels(pixels), m_Width(width), m_Height(height)
{ }

Texture Texture::Solid(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    std::vector<uint8_t> px { r, g, b, a };
    return Texture { px, 1, 1 };
}

std::vector<uint8_t>& Texture::GetPixels()
{
    return m_Pixels;
}

uint32_t Texture::GetWidth() const
{
    return m_Width;
}

uint32_t Texture::GetHeight() const
{
    return m_Height;
}

bool Texture::IsSrgb() const
{
    return m_Srgb;
}

void Texture::SetSrgb(bool srgb)
{
    m_Srgb = srgb;
}

Material::Material()
{
    auto& base = m_Textures[static_cast<size_t>(TextureSlot::BaseColor)];
    base = Texture::Solid(255, 255, 255, 255);
    base.SetSrgb(true);

    m_Textures[static_cast<size_t>(TextureSlot::MetallicRoughness)] =
        Texture::Solid(255, 255, 255, 255);

    m_Textures[static_cast<size_t>(TextureSlot::Normal)] =
        Texture::Solid(128, 128, 255, 255);

    auto& emissive = m_Textures[static_cast<size_t>(TextureSlot::Emissive)];
    emissive = Texture::Solid(0, 0, 0, 255);
    emissive.SetSrgb(true);

    m_Textures[static_cast<size_t>(TextureSlot::Occlusion)] =
        Texture::Solid(255, 255, 255, 255);
}

Texture& Material::GetTexture(TextureSlot slot)
{
    return m_Textures[static_cast<size_t>(slot)];
}

AllocatedImage& Material::GetImage(TextureSlot slot)
{
    return m_Images[static_cast<size_t>(slot)];
}

MaterialFactors& Material::GetFactors()
{
    return m_Factors;
}

const MaterialFactors& Material::GetFactors() const
{
    return m_Factors;
}