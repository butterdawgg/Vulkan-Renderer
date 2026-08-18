#ifndef FRAME_SYNC_H
#define FRAME_SYNC_H

// Groups the per-frame synchronisation primitives: image-available semaphores
// and in-flight fences are per frame-in-flight, render-finished semaphores are
// per swapchain image

#include <vulkan/vulkan.h>

#include <vector>

struct FrameSync
{
    std::vector<VkSemaphore> imageAvailableSemaphores { };
    std::vector<VkSemaphore> renderFinishedSemaphores { };
    std::vector<VkFence> inFlightFences { };

    void Init(VkDevice device, uint32_t framesInFlight, uint32_t swapchainImageCount);
    void Destroy(VkDevice device);
};

#endif // !FRAME_SYNC_H
