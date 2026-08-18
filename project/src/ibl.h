#ifndef IBL_H
#define IBL_H

// Image-Based Lighting precompute (diffuse irradiance only)

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <string>

struct VulkanContext;
struct CommandContext;

class IBL
{
    public:

    // Returns false on failure
    bool Generate(const VulkanContext& ctx, const CommandContext& cmdCtx,
        const std::string& equirectPath,
        VkShaderModule cubeVert, VkShaderModule equirectFrag, VkShaderModule irradianceFrag);

    void Destroy(const VulkanContext& ctx);

    bool IsValid() const { return m_Valid; }
    VkImageView IrradianceView() const { return m_IrradianceView; }
    VkSampler Sampler() const { return m_Sampler; }

    private:

    bool m_Valid { false };

    // Persistent outputs
    VkImage m_IrradianceImage { VK_NULL_HANDLE };
    VmaAllocation m_IrradianceAlloc { VK_NULL_HANDLE };
    VkImageView m_IrradianceView { VK_NULL_HANDLE };
    VkSampler m_Sampler { VK_NULL_HANDLE };

    static constexpr uint32_t ENV_SIZE = 512;
    static constexpr uint32_t IRRADIANCE_SIZE = 32;
    static constexpr VkFormat CUBE_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
};

#endif // !IBL_H