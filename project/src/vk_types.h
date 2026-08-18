#ifndef VK_TYPES_H
#define VK_TYPES_H

// Small POD types shared across the renderer modules

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <optional>
#include <vector>

// A VMA-allocated buffer
struct AllocatedBuffer
{
    VkBuffer buffer { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
};

// A VMA-allocated image, its (optional) view and metadata
struct AllocatedImage
{
    VkImage image { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };

    VkImageView view { VK_NULL_HANDLE };
    VkFormat format { VK_FORMAT_UNDEFINED };

    uint32_t width { 0 };
    uint32_t height { 0 };
    uint32_t mipLevels { 1 };
};

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool IsComplete() const
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities { };
    std::vector<VkSurfaceFormatKHR> formats { };
    std::vector<VkPresentModeKHR> presentModes { };
};

#endif // !VK_TYPES_H