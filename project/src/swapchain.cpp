#include "swapchain.h"

#include "vulkan_context.h"
#include "resource_manager.h"
#include "command_context.h"
#include "vk_utils.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

void Swapchain::CreateSwapchainOnly(const VulkanContext& ctx, GLFWwindow* window)
{
    CreateSwapchain(ctx, window);
    CreateSwapchainImageViews(ctx);
}

void Swapchain::CreateSwapchainResources(const VulkanContext& ctx, ResourceManager& resources,
    VkRenderPass renderPass, const CommandContext& cmdCtx)
{
    CreateDepthResources(ctx, resources, cmdCtx);
    CreateSwapchainFramebuffers(ctx, renderPass);
}

void Swapchain::Destroy(const VulkanContext& ctx, ResourceManager& resources)
{
    resources.DestroyImage(depthImage);

    for (auto framebuffer : swapchainFramebuffers)
        vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);

    for (auto imageView : swapchainImageViews)
        vkDestroyImageView(ctx.device, imageView, nullptr);

    vkDestroySwapchainKHR(ctx.device, swapchain, nullptr);

    swapchainFramebuffers.clear();
    swapchainImageViews.clear();
    swapchainImages.clear();
    swapchain = VK_NULL_HANDLE;
}

void Swapchain::Recreate(const VulkanContext& ctx, ResourceManager& resources,
    GLFWwindow* window, VkRenderPass renderPass, const CommandContext& cmdCtx)
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(ctx.device);

    Destroy(ctx, resources);
    CreateSwapchainOnly(ctx, window);
    CreateSwapchainResources(ctx, resources, renderPass, cmdCtx);
}

void Swapchain::CreateSwapchain(const VulkanContext& ctx, GLFWwindow* window)
{
    SwapChainSupportDetails swapChainSupport = ctx.QuerySwapChainSupport(ctx.physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR   presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D         extent = ChooseSwapExtent(swapChainSupport.capabilities, window);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount)
    {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainCreateInfo { };
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = ctx.surface;
    swapchainCreateInfo.minImageCount = imageCount;
    swapchainCreateInfo.imageFormat = surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent = extent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    auto queueIndices = ctx.FindQueueFamilies(ctx.physicalDevice);

    std::set<uint32_t> uniqueQueueFamilies {
        queueIndices.graphicsFamily.value(), queueIndices.presentFamily.value() };

    std::vector<uint32_t> queueFamilyIndices(
        uniqueQueueFamilies.begin(), uniqueQueueFamilies.end());

    if (queueIndices.graphicsFamily != queueIndices.presentFamily)
    {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainCreateInfo.queueFamilyIndexCount = 2;
        swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    }
    else
    {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swapchainCreateInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCreateInfo.presentMode = presentMode;
    swapchainCreateInfo.clipped = VK_TRUE;
    swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(ctx.device, &swapchainCreateInfo, nullptr, &swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain!");

    vkGetSwapchainImagesKHR(ctx.device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(ctx.device, swapchain, &imageCount, swapchainImages.data());

    swapchainImageFormat = surfaceFormat.format;
    swapchainExtent = extent;
}

void Swapchain::CreateSwapchainImageViews(const VulkanContext& ctx)
{
    swapchainImageViews.resize(swapchainImages.size());

    for (uint32_t i = 0; i < swapchainImages.size(); i++)
    {
        swapchainImageViews[i] =
            vkutil::CreateImageView(ctx.device, swapchainImages[i], swapchainImageFormat,
                VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

void Swapchain::CreateDepthResources(const VulkanContext& ctx, ResourceManager& resources,
    const CommandContext& cmdCtx)
{
    VkFormat depthFormat = ctx.FindDepthFormat();

    depthImage = resources.CreateImage(swapchainExtent.width, swapchainExtent.height, 1,
        depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    if (ctx.HasStencilComponent(depthFormat))
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    resources.CreateImageView(depthImage, aspectMask);

    cmdCtx.ImmediateSubmit(ctx, [&](VkCommandBuffer cmd)
    {
        vkutil::TransitionImageLayout(cmd, depthImage.image,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_NONE,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            aspectMask,
            depthImage.mipLevels);
    });
}

void Swapchain::CreateSwapchainFramebuffers(const VulkanContext& ctx, VkRenderPass renderPass)
{
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++)
    {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[i],
            depthImage.view
        };

        VkFramebufferCreateInfo framebufferInfo { };
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapchainExtent.width;
        framebufferInfo.height = swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(ctx.device, &framebufferInfo, nullptr,
            &swapchainFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer!");
    }
}

VkSurfaceFormatKHR Swapchain::ChooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const auto& availableFormat : availableFormats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return availableFormat;
    }
    return availableFormats[0];
}

VkPresentModeKHR Swapchain::ChooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for (const auto& mode : availablePresentModes)
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            return mode;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actualExtent.width = std::clamp(actualExtent.width,
        capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
        capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}
