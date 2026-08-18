#ifndef VK_UTILS_H
#define VK_UTILS_H

#include <vulkan/vulkan.h>

#include "vk_types.h"

namespace vkutil
{
    // Creates a fresh image view (used for swapchain images that the swapchain owns directly)
    VkImageView CreateImageView(
        VkDevice device,
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspectFlags,
        uint32_t mipLevels);

    // Records a pipeline barrier that changes an image's layout
    void TransitionImageLayout(
        VkCommandBuffer cmd,
        VkImage image,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask,
        uint32_t mipLevels = 1);

    void CopyBuffer(
        VkCommandBuffer cmd,
        VkBuffer src,
        VkBuffer dst,
        VkDeviceSize size);

    // Copies the staging buffer into mip level 0 of the image, which must already
    // be in TRANSFER_DST_OPTIMAL
    void CopyBufferToImage(
        VkCommandBuffer cmd,
        VkBuffer buffer,
        const AllocatedImage& image);

    // Records the full mip-chain blit; on entry every mip must be in
    // TRANSFER_DST_OPTIMAL; on exit every mip is in SHADER_READ_ONLY_OPTIMAL
    void GenerateMipmaps(
        VkCommandBuffer cmd,
        const AllocatedImage& image);
}

#endif // !VK_UTILS_H
