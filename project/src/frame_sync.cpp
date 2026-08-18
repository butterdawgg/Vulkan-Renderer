#include "frame_sync.h"

#include <stdexcept>

void FrameSync::Init(VkDevice device, uint32_t framesInFlight, uint32_t swapchainImageCount)
{
    imageAvailableSemaphores.resize(framesInFlight);
    renderFinishedSemaphores.resize(swapchainImageCount);
    inFlightFences.resize(framesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo { };
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo { };
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < imageAvailableSemaphores.size(); i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
            &imageAvailableSemaphores[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create semaphores!");
    }

    for (uint32_t i = 0; i < renderFinishedSemaphores.size(); i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
            &renderFinishedSemaphores[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create semaphores!");
    }

    for (uint32_t i = 0; i < inFlightFences.size(); i++)
    {
        if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create fences!");
    }
}

void FrameSync::Destroy(VkDevice device)
{
    for (auto semaphore : imageAvailableSemaphores)
        vkDestroySemaphore(device, semaphore, nullptr);

    for (auto semaphore : renderFinishedSemaphores)
        vkDestroySemaphore(device, semaphore, nullptr);

    for (auto fence : inFlightFences)
        vkDestroyFence(device, fence, nullptr);
}