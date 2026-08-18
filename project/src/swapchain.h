#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

// Owns the swapchain, its images/views, the depth buffer and the framebuffers

#include <vulkan/vulkan.h>

#include <vector>

#include "vk_types.h"

struct VulkanContext;
class ResourceManager;
struct CommandContext;
struct GLFWwindow;

struct Swapchain
{
    VkSwapchainKHR swapchain { };
    std::vector<VkImage> swapchainImages { };
    VkFormat swapchainImageFormat { };
    VkExtent2D swapchainExtent { };
    std::vector<VkImageView> swapchainImageViews { };
    AllocatedImage depthImage { };
    std::vector<VkFramebuffer> swapchainFramebuffers { };

    // Creates the swapchain and its image views
    void CreateSwapchainOnly(const VulkanContext& ctx, GLFWwindow* window);

    // Creates the depth buffer and framebuffers, which require the render pass
    void CreateSwapchainResources(const VulkanContext& ctx, ResourceManager& resources,
        VkRenderPass renderPass, const CommandContext& cmdCtx);

    void Destroy(const VulkanContext& ctx, ResourceManager& resources);

    void Recreate(const VulkanContext& ctx, ResourceManager& resources,
        GLFWwindow* window, VkRenderPass renderPass, const CommandContext& cmdCtx);

    private:

    void CreateSwapchain(const VulkanContext& ctx, GLFWwindow* window);

    void CreateSwapchainImageViews(const VulkanContext& ctx);

    void CreateDepthResources(const VulkanContext& ctx, ResourceManager& resources,
        const CommandContext& cmdCtx);

    void CreateSwapchainFramebuffers(const VulkanContext& ctx, VkRenderPass renderPass);

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats);

    VkPresentModeKHR ChooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes);

    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window);
};

#endif // !SWAPCHAIN_H