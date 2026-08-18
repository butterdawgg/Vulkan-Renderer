#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

// Central owner for VMA-allocated GPU resources

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <vector>

#include "vk_types.h"

struct VulkanContext;

class ResourceManager
{
    public:

    explicit ResourceManager(VulkanContext* context);
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // Buffers

    AllocatedBuffer CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaMemoryUsage memoryUsage,
        VmaAllocationCreateFlags createFlags);

    // Destroys a tracked buffer and clears the caller's handle
    void DestroyBuffer(AllocatedBuffer& buffer);

    // Images

    AllocatedImage CreateImage(
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels,
        VkFormat format,
        VkImageUsageFlags usage);

    // Creates a view for an already-created AllocatedImage, writes it into
    // image.view, and keeps the tracked copy in sync so the view is destroyed
    // together with the image
    void CreateImageView(AllocatedImage& image, VkImageAspectFlags aspectFlags);

    // Destroys a tracked image (and its view) and clears the caller's handle
    void DestroyImage(AllocatedImage& image);

    // Frees everything still tracked; called automatically by the destructor,
    // can also be called explicitly during ordered shutdown
    void DestroyAll();

    private:

    VulkanContext* m_Context { nullptr };

    std::vector<AllocatedBuffer> m_Buffers { };
    std::vector<AllocatedImage> m_Images { };
};

#endif // !RESOURCE_MANAGER_H