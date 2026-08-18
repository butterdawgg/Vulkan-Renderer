#include "command_context.h"
#include "vulkan_context.h"

#include <stdexcept>

void CommandContext::Init(const VulkanContext& ctx, uint32_t framesInFlight)
{
    auto queueIndices = ctx.FindQueueFamilies(ctx.physicalDevice);

    VkCommandPoolCreateInfo poolInfo { };
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueIndices.graphicsFamily.value();

    if (vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool!");

    commandBuffers.resize(framesInFlight);

    VkCommandBufferAllocateInfo allocInfo { };
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(ctx.device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate command buffers!");

    // Immediate-submit objects

    VkCommandPoolCreateInfo immediatePoolInfo { };
    immediatePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    immediatePoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    immediatePoolInfo.queueFamilyIndex = queueIndices.graphicsFamily.value();

    if (vkCreateCommandPool(ctx.device, &immediatePoolInfo, nullptr, &immediatePool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create immediate command pool!");

    VkCommandBufferAllocateInfo immediateAllocInfo { };
    immediateAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    immediateAllocInfo.commandPool = immediatePool;
    immediateAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    immediateAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(ctx.device, &immediateAllocInfo, &immediateCmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate immediate command buffer!");

    // Created unsignaled: ImmediateSubmit resets it after each submit
    VkFenceCreateInfo fenceInfo { };
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    if (vkCreateFence(ctx.device, &fenceInfo, nullptr, &immediateFence) != VK_SUCCESS)
        throw std::runtime_error("Failed to create immediate fence!");
}

void CommandContext::Destroy(VkDevice device)
{
    vkDestroyFence(device, immediateFence, nullptr);
    vkDestroyCommandPool(device, immediatePool, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
}

void CommandContext::ImmediateSubmit(const VulkanContext& ctx,
    const std::function<void(VkCommandBuffer)>& record) const
{
    vkResetFences(ctx.device, 1, &immediateFence);
    vkResetCommandBuffer(immediateCmd, 0);

    VkCommandBufferBeginInfo beginInfo { };
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(immediateCmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin immediate command buffer!");

    record(immediateCmd);

    if (vkEndCommandBuffer(immediateCmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to end immediate command buffer!");

    VkSubmitInfo submitInfo { };
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &immediateCmd;

    if (vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, immediateFence) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit immediate command buffer!");

    vkWaitForFences(ctx.device, 1, &immediateFence, VK_TRUE, UINT64_MAX);
}
