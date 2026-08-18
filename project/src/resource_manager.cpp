#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "resource_manager.h"
#include "vulkan_context.h"

#include <algorithm>
#include <stdexcept>

ResourceManager::ResourceManager(VulkanContext* context) :
    m_Context(context)
{ }

ResourceManager::~ResourceManager()
{
    DestroyAll();
}

AllocatedBuffer ResourceManager::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags createFlags)
{
    AllocatedBuffer result { };

    VkBufferCreateInfo bufferInfo { };
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = memoryUsage;
    allocInfo.flags = createFlags;

    if (vmaCreateBuffer(m_Context->allocator, &bufferInfo, &allocInfo,
        &result.buffer, &result.allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create VMA buffer");

    m_Buffers.push_back(result);

    return result;
}

void ResourceManager::DestroyBuffer(AllocatedBuffer& buffer)
{
    if (buffer.buffer == VK_NULL_HANDLE)
        return;

    auto it = std::find_if(m_Buffers.begin(), m_Buffers.end(),
        [&](const AllocatedBuffer& tracked) { return tracked.buffer == buffer.buffer; });

    if (it != m_Buffers.end())
    {
        vmaDestroyBuffer(m_Context->allocator, it->buffer, it->allocation);
        m_Buffers.erase(it);
    }

    buffer.buffer = VK_NULL_HANDLE;
    buffer.allocation = VK_NULL_HANDLE;
}

AllocatedImage ResourceManager::CreateImage(
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkFormat format,
    VkImageUsageFlags usage)
{
    AllocatedImage result { };
    result.width = width;
    result.height = height;
    result.mipLevels = mipLevels;
    result.format = format;

    VkImageCreateInfo imageInfo { };
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo { };
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(m_Context->allocator, &imageInfo, &allocInfo,
        &result.image, &result.allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create image");

    m_Images.push_back(result);

    return result;
}

void ResourceManager::CreateImageView(AllocatedImage& image, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo { };
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = image.format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = image.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Context->device, &viewInfo, nullptr, &image.view) != VK_SUCCESS)
        throw std::runtime_error("failed to create image view");

    // Sync the view handle into our tracked copy so DestroyImage frees it too.
    auto it = std::find_if(m_Images.begin(), m_Images.end(),
        [&](const AllocatedImage& tracked) { return tracked.image == image.image; });

    if (it != m_Images.end())
        it->view = image.view;
}

void ResourceManager::DestroyImage(AllocatedImage& image)
{
    if (image.image == VK_NULL_HANDLE)
        return;

    auto it = std::find_if(m_Images.begin(), m_Images.end(),
        [&](const AllocatedImage& tracked) { return tracked.image == image.image; });

    if (it != m_Images.end())
    {
        if (it->view != VK_NULL_HANDLE)
            vkDestroyImageView(m_Context->device, it->view, nullptr);

        vmaDestroyImage(m_Context->allocator, it->image, it->allocation);
        m_Images.erase(it);
    }

    image.view = VK_NULL_HANDLE;
    image.image = VK_NULL_HANDLE;
    image.allocation = VK_NULL_HANDLE;
}

void ResourceManager::DestroyAll()
{
    for (auto& image : m_Images)
    {
        if (image.view != VK_NULL_HANDLE)
            vkDestroyImageView(m_Context->device, image.view, nullptr);

        if (image.image != VK_NULL_HANDLE)
            vmaDestroyImage(m_Context->allocator, image.image, image.allocation);
    }
    m_Images.clear();

    for (auto& buffer : m_Buffers)
    {
        if (buffer.buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_Context->allocator, buffer.buffer, buffer.allocation);
    }
    m_Buffers.clear();
}