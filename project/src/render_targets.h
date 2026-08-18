#ifndef RENDER_TARGETS_H
#define RENDER_TARGETS_H

// Owns all the offscreen attachments and render passes the deferred pipeline
// needs, and keeps their lifetimes together

#include <vulkan/vulkan.h>

#include "vk_types.h"

struct VulkanContext;
class ResourceManager;
struct CommandContext;

class RenderTargets
{
    public:

    // Attachment formats.
    static constexpr VkFormat ALBEDO_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;
    static constexpr VkFormat NORMAL_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat METALROUGH_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat EMISSIVE_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat HDR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat SHADOW_FORMAT = VK_FORMAT_D32_SFLOAT;

    static constexpr uint32_t GBUFFER_COLOR_COUNT = 4;
    static constexpr uint32_t DEFAULT_SHADOW_RESOLUTION = 2048;

    void SetShadowResolution(uint32_t resolution) { m_ShadowResolution = resolution; }

    void CreateRenderPasses(const VulkanContext& ctx);
    void DestroyRenderPasses(const VulkanContext& ctx);

    void CreateSizedResources(const VulkanContext& ctx, ResourceManager& resources,
        VkExtent2D extent);

    void DestroySizedResources(const VulkanContext& ctx, ResourceManager& resources);

    // Render passes
    VkRenderPass ShadowPass() const { return m_ShadowPass; }
    VkRenderPass PrepassPass() const { return m_PrepassPass; }
    VkRenderPass GBufferPass() const { return m_GBufferPass; }
    VkRenderPass LightingPass() const { return m_LightingPass; }

    // Framebuffers
    VkFramebuffer ShadowFramebuffer() const { return m_ShadowFB; }
    VkFramebuffer PrepassFramebuffer() const { return m_PrepassFB; }
    VkFramebuffer GBufferFramebuffer() const { return m_GBufferFB; }
    VkFramebuffer LightingFramebuffer() const { return m_LightingFB; }

    // Sampled views (for binding as textures in later passes)
    VkImageView ShadowView() const { return m_ShadowDepth.view; }
    VkImageView DepthView() const { return m_Depth.view; }
    VkImageView AlbedoView() const { return m_Albedo.view; }
    VkImageView NormalView() const { return m_Normal.view; }
    VkImageView MetalRoughView() const { return m_MetalRough.view; }
    VkImageView EmissiveView() const { return m_Emissive.view; }
    VkImageView HdrView() const { return m_Hdr.view; }

    VkExtent2D Extent() const { return m_Extent; }
    VkExtent2D ShadowExtent() const { return { m_ShadowResolution, m_ShadowResolution }; }
    VkFormat DepthFormat() const { return m_DepthFormat; }

    private:

    VkRenderPass m_ShadowPass { VK_NULL_HANDLE };
    VkRenderPass m_PrepassPass { VK_NULL_HANDLE };
    VkRenderPass m_GBufferPass { VK_NULL_HANDLE };
    VkRenderPass m_LightingPass { VK_NULL_HANDLE };

    VkFramebuffer m_ShadowFB { VK_NULL_HANDLE };
    VkFramebuffer m_PrepassFB { VK_NULL_HANDLE };
    VkFramebuffer m_GBufferFB { VK_NULL_HANDLE };
    VkFramebuffer m_LightingFB { VK_NULL_HANDLE };

    AllocatedImage m_ShadowDepth { };
    AllocatedImage m_Depth { };
    AllocatedImage m_Albedo { };
    AllocatedImage m_Normal { };
    AllocatedImage m_MetalRough { };
    AllocatedImage m_Emissive { };
    AllocatedImage m_Hdr { };

    VkExtent2D m_Extent { };
    VkFormat m_DepthFormat { VK_FORMAT_D32_SFLOAT };
    uint32_t m_ShadowResolution { DEFAULT_SHADOW_RESOLUTION };
};

#endif // !RENDER_TARGETS_H