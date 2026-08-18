#ifndef VULKAN_CONTEXT_H
#define VULKAN_CONTEXT_H

// Owns the core Vulkan objects: instance, surface, physical/logical device,
// queues and the VMA allocator

// It also answers physical-device queries
// (queue families, swapchain support, format selection)

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <vector>

#include "vk_types.h"

struct GLFWwindow;

struct VulkanContext
{
    VkInstance instance { };
    VkSurfaceKHR surface { };
    VkPhysicalDevice physicalDevice { VK_NULL_HANDLE };
    VkDevice device { };
    VkQueue graphicsQueue { };
    VkQueue presentQueue { };
    VmaAllocator allocator { };

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    #ifdef NDEBUG
    const bool enableValidationLayers = false;
    #else
    const bool enableValidationLayers = true;
    #endif

    VkDebugUtilsMessengerEXT debugMessenger { };

    void Init(GLFWwindow* window);
    void Destroy();

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice dev) const;
    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice dev) const;

    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates,
        VkImageTiling tiling, VkFormatFeatureFlags features) const;

    VkFormat FindDepthFormat() const;
    bool HasStencilComponent(VkFormat format) const;

    private:

    void CreateInstance();
    void SetupDebugMessenger();
    void CreateWindowSurface(GLFWwindow* window);
    void SetupPhysicalDevice();
    void CreateLogicalDevice();
    void CreateAllocator();

    int RateDeviceSuitability(VkPhysicalDevice dev);
    VkPhysicalDevice ChoosePhysicalDevice(const std::vector<VkPhysicalDevice>& devices);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice dev);
    bool IsSwapchainAdequate(VkPhysicalDevice dev);
    bool CheckValidationLayersSupport();
    std::vector<const char*> GetRequiredExtensions();
    VkDebugUtilsMessengerCreateInfoEXT GetDebugMessengerCreateInfo();

    static VkResult CreateDebugUtilsMessengerEXT(VkInstance inst,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger);

    static void DestroyDebugUtilsMessengerEXT(VkInstance inst,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks* pAllocator);

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};

#endif // !VULKAN_CONTEXT_H