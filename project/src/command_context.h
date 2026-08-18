#ifndef COMMAND_CONTEXT_H
#define COMMAND_CONTEXT_H

// Owns the graphics command pool and the per-frame primary command buffers used
// by the render loop, plus a small dedicated pool/buffer/fence for one-off
// uploads

#include <vulkan/vulkan.h>

#include <functional>
#include <vector>

struct VulkanContext;

struct CommandContext
{
    VkCommandPool commandPool { };
    std::vector<VkCommandBuffer> commandBuffers { };

    // Dedicated objects for immediate (blocking) uploads
    VkCommandPool immediatePool { };
    VkCommandBuffer immediateCmd { };
    VkFence immediateFence { };

    void Init(const VulkanContext& ctx, uint32_t framesInFlight);
    void Destroy(VkDevice device);

    // Records the caller's commands into immediateCmd and submits them as a
    // single batch, blocking on immediateFence until the GPU is done
    void ImmediateSubmit(const VulkanContext& ctx,
        const std::function<void(VkCommandBuffer)>& record) const;
};

#endif // !COMMAND_CONTEXT_H
