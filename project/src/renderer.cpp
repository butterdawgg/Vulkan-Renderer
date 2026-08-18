#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "renderer.h"

#include "vulkan_context.h"
#include "resource_manager.h"
#include "command_context.h"
#include "swapchain.h"
#include "frame_sync.h"
#include "render_targets.h"
#include "ibl.h"
#include "pipeline_builder.h"
#include "vk_utils.h"

#include "scene.h"
#include "mesh.h"
#include "material.h"
#include "camera.h"
#include "lighting.h"
#include "debug_utils.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t FRAMES_IN_FLIGHT = 2;
    const std::string SHADER_DIR = "resources/shaders/";

    // Push-constant / UBO layouts

    struct CameraUBO
    {
        glm::mat4 view;
        glm::mat4 proj;
    };

    struct GeometryPush
    {
        glm::mat4 model;
        glm::vec4 baseColorFactor;
        glm::vec4 emissiveFactor;
        float metallicFactor;
        float roughnessFactor;
        float normalScale;
        float occlusionStrength;
    };

    struct DepthPush
    {
        glm::mat4 model;
    };

    struct ShadowPush
    {
        glm::mat4 lightViewProj;
        glm::mat4 model;
    };

    struct GpuPointLight
    {
        glm::vec4 position; // xyz, w = range
        glm::vec4 color;    // rgb, w = intensity
    };

    struct LightingUBO
    {
        glm::mat4 invViewProj;
        glm::mat4 lightViewProj;
        glm::vec4 camPos;
        glm::vec4 dirDirection;
        glm::vec4 dirColor; // w = lux
        glm::vec4 ambient; // w = hasIBL
        glm::vec4 params; // x=numPoints, y=shadowBias, z=iblIntensity
        GpuPointLight points[MAX_POINT_LIGHTS];
    };

    std::vector<char> ReadFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open())
            throw std::runtime_error("Failed to open shader file: " + path);

        size_t size = static_cast<size_t>(file.tellg());

        std::vector<char> buffer(size);

        file.seekg(0);
        file.read(buffer.data(), size);

        return buffer;
    }

    VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo ci { };

        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule module { VK_NULL_HANDLE };

        if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shader module");

        return module;
    }

    VkShaderModule LoadShader(VkDevice device, const std::string& name)
    {
        return CreateShaderModule(device, ReadFile(SHADER_DIR + name + ".spv"));
    }
}



// ----------------------
// --- Renderer::Impl ---
// ----------------------

class Renderer::Impl
{
    public:

    Impl(GLFWwindow* window, const RenderSettings& settings);
    ~Impl();

    void LoadScene(Scene& scene);
    void DrawFrame(Camera& camera);
    void DeviceWaitIdle();

    RenderSettings& GetSettings() { return m_Settings; }

    private:

    GLFWwindow* m_Window { nullptr };

    VulkanContext m_Ctx { };
    ResourceManager m_Resources { &m_Ctx }; // destroyed before m_Ctx
    CommandContext m_CmdCtx { };
    Swapchain m_Swapchain { };
    FrameSync m_Sync { };
    RenderTargets m_Targets { };
    IBL m_IBL { };

    Scene* m_Scene { nullptr };

    // Post (swapchain) render pass + framebuffers
    VkRenderPass m_PostPass { VK_NULL_HANDLE };
    std::vector<VkFramebuffer> m_PostFramebuffers { };

    // Descriptor set layouts
    VkDescriptorSetLayout m_CameraSetLayout { VK_NULL_HANDLE };
    VkDescriptorSetLayout m_MaterialSetLayout { VK_NULL_HANDLE };
    VkDescriptorSetLayout m_LightingSetLayout { VK_NULL_HANDLE };
    VkDescriptorSetLayout m_PostSetLayout { VK_NULL_HANDLE };

    // Pipeline layouts
    VkPipelineLayout m_PrepassLayout { VK_NULL_HANDLE };
    VkPipelineLayout m_GBufferLayout { VK_NULL_HANDLE };
    VkPipelineLayout m_ShadowLayout { VK_NULL_HANDLE };
    VkPipelineLayout m_LightingLayout { VK_NULL_HANDLE };
    VkPipelineLayout m_PostLayout { VK_NULL_HANDLE };

    // Pipelines
    VkPipeline m_PrepassPipeline { VK_NULL_HANDLE };
    VkPipeline m_GBufferPipeline { VK_NULL_HANDLE };
    VkPipeline m_ShadowPipeline { VK_NULL_HANDLE };
    VkPipeline m_LightingPipeline { VK_NULL_HANDLE };
    VkPipeline m_PostPipeline { VK_NULL_HANDLE };

    // Samplers
    VkSampler m_MaterialSampler { VK_NULL_HANDLE }; // linear, repeat, mips
    VkSampler m_GBufferSampler { VK_NULL_HANDLE }; // nearest, clamp
    VkSampler m_ShadowSampler { VK_NULL_HANDLE }; // linear, clamp, border

    // 1x1 fallback cubemap bound to the irradiance slot when IBL is disabled
    VkImage m_DummyCubeImage { VK_NULL_HANDLE };
    VmaAllocation m_DummyCubeAlloc { VK_NULL_HANDLE };
    VkImageView m_DummyCubeView { VK_NULL_HANDLE };

    // Per-frame UBOs
    std::array<AllocatedBuffer, FRAMES_IN_FLIGHT> m_CameraUBOs { };
    std::array<AllocatedBuffer, FRAMES_IN_FLIGHT> m_LightingUBOs { };

    // Descriptors
    VkDescriptorPool m_DescriptorPool { VK_NULL_HANDLE };
    std::array<VkDescriptorSet, FRAMES_IN_FLIGHT> m_CameraSets { };
    std::array<VkDescriptorSet, FRAMES_IN_FLIGHT> m_LightingSets { };
    VkDescriptorSet m_PostSet { VK_NULL_HANDLE };
    std::vector<VkDescriptorSet> m_MaterialSets { };

    uint32_t m_CurrentFrame { 0 };
    bool m_FramebufferResized { false };

    RenderSettings m_Settings { };

    // Setup
    void CreatePostPass();
    void CreatePostFramebuffers();
    void DestroyPostFramebuffers();
    void CreateSamplers();
    void CreateDummyCube();
    void CreateDescriptorSetLayouts();
    void CreatePipelineLayouts();
    void CreatePipelines();
    void CreateUniformBuffers();
    void RunIBLPrecompute();

    // Per-scene
    void UploadMesh(Mesh& mesh);
    void UploadMaterial(Material& material);
    void CreateDescriptorPoolAndSets();

    // Per-frame
    void UpdateCameraUBO(uint32_t frame, Camera& camera);
    void UpdateLightingUBO(uint32_t frame, Camera& camera, const glm::mat4& lightViewProj);
    void RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, Camera& camera);

    void RecreateSwapchain();

    glm::mat4 ComputeLightViewProj(const DirectionalLight& light) const;

    // Vertex input description for the full Vertex layout
    static VkVertexInputBindingDescription VertexBinding();
    static std::array<VkVertexInputAttributeDescription, 4> VertexAttributes();
    // Position-only input for the depth-only passes (prepass, shadow)
    static std::array<VkVertexInputAttributeDescription, 1> PositionOnlyAttribute();
};



// ----------------------------------
// --- Construction / destruction ---
// ----------------------------------

Renderer::Impl::Impl(GLFWwindow* window, const RenderSettings& settings)
    : m_Window(window), m_Settings(settings)
{
    m_Ctx.Init(window);
    m_CmdCtx.Init(m_Ctx, FRAMES_IN_FLIGHT);

    m_Swapchain.CreateSwapchainOnly(m_Ctx, window);

    CreatePostPass();

    m_Targets.SetShadowResolution(m_Settings.shadowMapResolution);
    m_Targets.CreateRenderPasses(m_Ctx);
    m_Targets.CreateSizedResources(m_Ctx, m_Resources, m_Swapchain.swapchainExtent);

    CreatePostFramebuffers();

    m_Sync.Init(m_Ctx.device, FRAMES_IN_FLIGHT,
        static_cast<uint32_t>(m_Swapchain.swapchainImages.size()));

    CreateSamplers();
    CreateDummyCube();
    CreateDescriptorSetLayouts();
    CreatePipelineLayouts();
    CreatePipelines();
    CreateUniformBuffers();
}

Renderer::Impl::~Impl()
{
    vkDeviceWaitIdle(m_Ctx.device);

    if (m_DescriptorPool)
        vkDestroyDescriptorPool(m_Ctx.device, m_DescriptorPool, nullptr);

    for (auto& b : m_LightingUBOs) m_Resources.DestroyBuffer(b);
    for (auto& b : m_CameraUBOs)   m_Resources.DestroyBuffer(b);

    if (m_PostPipeline) vkDestroyPipeline(m_Ctx.device, m_PostPipeline, nullptr);
    if (m_LightingPipeline) vkDestroyPipeline(m_Ctx.device, m_LightingPipeline, nullptr);
    if (m_ShadowPipeline) vkDestroyPipeline(m_Ctx.device, m_ShadowPipeline, nullptr);
    if (m_GBufferPipeline) vkDestroyPipeline(m_Ctx.device, m_GBufferPipeline, nullptr);
    if (m_PrepassPipeline) vkDestroyPipeline(m_Ctx.device, m_PrepassPipeline, nullptr);

    if (m_PostLayout) vkDestroyPipelineLayout(m_Ctx.device, m_PostLayout, nullptr);
    if (m_LightingLayout) vkDestroyPipelineLayout(m_Ctx.device, m_LightingLayout, nullptr);
    if (m_ShadowLayout) vkDestroyPipelineLayout(m_Ctx.device, m_ShadowLayout, nullptr);
    if (m_GBufferLayout) vkDestroyPipelineLayout(m_Ctx.device, m_GBufferLayout, nullptr);
    if (m_PrepassLayout) vkDestroyPipelineLayout(m_Ctx.device, m_PrepassLayout, nullptr);

    if (m_PostSetLayout) vkDestroyDescriptorSetLayout(m_Ctx.device, m_PostSetLayout, nullptr);
    if (m_LightingSetLayout) vkDestroyDescriptorSetLayout(m_Ctx.device, m_LightingSetLayout, nullptr);
    if (m_MaterialSetLayout) vkDestroyDescriptorSetLayout(m_Ctx.device, m_MaterialSetLayout, nullptr);
    if (m_CameraSetLayout) vkDestroyDescriptorSetLayout(m_Ctx.device, m_CameraSetLayout, nullptr);

    if (m_ShadowSampler) vkDestroySampler(m_Ctx.device, m_ShadowSampler, nullptr);
    if (m_GBufferSampler) vkDestroySampler(m_Ctx.device, m_GBufferSampler, nullptr);
    if (m_MaterialSampler) vkDestroySampler(m_Ctx.device, m_MaterialSampler, nullptr);

    if (m_DummyCubeView) vkDestroyImageView(m_Ctx.device, m_DummyCubeView, nullptr);
    if (m_DummyCubeImage) vmaDestroyImage(m_Ctx.allocator, m_DummyCubeImage, m_DummyCubeAlloc);

    m_IBL.Destroy(m_Ctx);

    DestroyPostFramebuffers();
    if (m_PostPass) vkDestroyRenderPass(m_Ctx.device, m_PostPass, nullptr);

    m_Targets.DestroySizedResources(m_Ctx, m_Resources);
    m_Targets.DestroyRenderPasses(m_Ctx);

    m_Sync.Destroy(m_Ctx.device);
    m_Swapchain.Destroy(m_Ctx, m_Resources);
    m_CmdCtx.Destroy(m_Ctx.device);

    m_Resources.DestroyAll();
    m_Ctx.Destroy();
}

void Renderer::Impl::DeviceWaitIdle()
{
    vkDeviceWaitIdle(m_Ctx.device);
}



// --------------------
// --- Vertex input ---
// --------------------

VkVertexInputBindingDescription Renderer::Impl::VertexBinding()
{
    VkVertexInputBindingDescription binding { };
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::array<VkVertexInputAttributeDescription, 4> Renderer::Impl::VertexAttributes()
{
    std::array<VkVertexInputAttributeDescription, 4> attrs { };
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent) };
    attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) };
    return attrs;
}

std::array<VkVertexInputAttributeDescription, 1> Renderer::Impl::PositionOnlyAttribute()
{
    std::array<VkVertexInputAttributeDescription, 1> attrs { };

    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) };

    return attrs;
}



// ---------------------------------------------------
// --- Post render pass + framebuffers (swapchain) ---
// ---------------------------------------------------

void Renderer::Impl::CreatePostPass()
{
    VkAttachmentDescription color { };
    color.format = m_Swapchain.swapchainImageFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass { };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkSubpassDependency dep { };
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp { };
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;

    if (vkCreateRenderPass(m_Ctx.device, &rp, nullptr, &m_PostPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create post render pass");
}

void Renderer::Impl::CreatePostFramebuffers()
{
    m_PostFramebuffers.resize(m_Swapchain.swapchainImageViews.size());

    for (size_t i = 0; i < m_Swapchain.swapchainImageViews.size(); i++)
    {
        VkFramebufferCreateInfo fb { };

        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = m_PostPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &m_Swapchain.swapchainImageViews[i];
        fb.width = m_Swapchain.swapchainExtent.width;
        fb.height = m_Swapchain.swapchainExtent.height;
        fb.layers = 1;

        if (vkCreateFramebuffer(m_Ctx.device, &fb, nullptr, &m_PostFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create post framebuffer");
    }
}

void Renderer::Impl::DestroyPostFramebuffers()
{
    for (auto fb : m_PostFramebuffers)
        if (fb) vkDestroyFramebuffer(m_Ctx.device, fb, nullptr);

    m_PostFramebuffers.clear();
}



// ----------------
// --- Samplers ---
// ----------------

void Renderer::Impl::CreateSamplers()
{
    VkSamplerCreateInfo s { };
    s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter = s.minFilter = VK_FILTER_LINEAR;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    s.anisotropyEnable = VK_FALSE;
    s.maxLod = VK_LOD_CLAMP_NONE;
    vkCreateSampler(m_Ctx.device, &s, nullptr, &m_MaterialSampler);

    VkSamplerCreateInfo g = s;
    g.magFilter = g.minFilter = VK_FILTER_NEAREST;
    g.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    g.addressModeU = g.addressModeV = g.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(m_Ctx.device, &g, nullptr, &m_GBufferSampler);

    VkSamplerCreateInfo sh = s;
    sh.magFilter = sh.minFilter = VK_FILTER_NEAREST; // manual PCF wants exact texel depths
    sh.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sh.addressModeU = sh.addressModeV = sh.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sh.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // outside shadow map = lit
    vkCreateSampler(m_Ctx.device, &sh, nullptr, &m_ShadowSampler);
}

void Renderer::Impl::CreateDummyCube()
{
    VkImageCreateInfo ii { };
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ii.extent = { 1, 1, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 6;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai { };
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    vmaCreateImage(m_Ctx.allocator, &ii, &ai, &m_DummyCubeImage, &m_DummyCubeAlloc, nullptr);

    // Transition all 6 layers to a sampleable layout (contents stay undefined;
    // the lighting shader never uses them when IBL is disabled)
    m_CmdCtx.ImmediateSubmit(m_Ctx, [&] (VkCommandBuffer cmd)
        {
            VkImageMemoryBarrier barrier { };
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_DummyCubeImage;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });

    VkImageViewCreateInfo vi { };
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = m_DummyCubeImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    vi.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
    vkCreateImageView(m_Ctx.device, &vi, nullptr, &m_DummyCubeView);
}



// ------------------------------
// --- Descriptor set layouts ---
// ------------------------------

void Renderer::Impl::CreateDescriptorSetLayouts()
{
    auto makeLayout = [&] (const std::vector<VkDescriptorSetLayoutBinding>& bindings)
        {
            VkDescriptorSetLayoutCreateInfo ci { };

            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            ci.bindingCount = static_cast<uint32_t>(bindings.size());
            ci.pBindings = bindings.data();

            VkDescriptorSetLayout layout { VK_NULL_HANDLE };

            if (vkCreateDescriptorSetLayout(m_Ctx.device, &ci, nullptr, &layout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create descriptor set layout");

            return layout;
        };

    auto ubo = [] (uint32_t b, VkShaderStageFlags stage)
        {
            return VkDescriptorSetLayoutBinding { b, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stage, nullptr };
        };

    auto sampler = [] (uint32_t b, VkShaderStageFlags stage)
        {
            return VkDescriptorSetLayoutBinding { b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stage, nullptr };
        };

    // Camera (set 0 for geometry passes)
    m_CameraSetLayout = makeLayout({ ubo(0, VK_SHADER_STAGE_VERTEX_BIT) });

    // Material (set 1): 5 textures
    m_MaterialSetLayout = makeLayout({
        sampler(0, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(1, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(2, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(3, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(4, VK_SHADER_STAGE_FRAGMENT_BIT),
        });

        // Lighting (set 0): 5 g-buffer + shadow + irradiance + lighting UBO
    m_LightingSetLayout = makeLayout({
        sampler(0, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(1, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(2, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(3, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(4, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(5, VK_SHADER_STAGE_FRAGMENT_BIT),
        sampler(6, VK_SHADER_STAGE_FRAGMENT_BIT),
        ubo(7, VK_SHADER_STAGE_FRAGMENT_BIT),
        });

        // Post (set 0): HDR colour.
    m_PostSetLayout = makeLayout({ sampler(0, VK_SHADER_STAGE_FRAGMENT_BIT) });
}

void Renderer::Impl::CreatePipelineLayouts()
{
    auto makeLayout = [&] (const std::vector<VkDescriptorSetLayout>& sets,
        const std::vector<VkPushConstantRange>& pushes)
        {
            VkPipelineLayoutCreateInfo ci { };

            ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            ci.setLayoutCount = static_cast<uint32_t>(sets.size());
            ci.pSetLayouts = sets.empty() ? nullptr : sets.data();
            ci.pushConstantRangeCount = static_cast<uint32_t>(pushes.size());
            ci.pPushConstantRanges = pushes.empty() ? nullptr : pushes.data();

            VkPipelineLayout layout { VK_NULL_HANDLE };

            if (vkCreatePipelineLayout(m_Ctx.device, &ci, nullptr, &layout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create pipeline layout");

            return layout;
        };

    m_PrepassLayout = makeLayout({ m_CameraSetLayout },
        { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DepthPush) } });

    m_GBufferLayout = makeLayout({ m_CameraSetLayout, m_MaterialSetLayout },
        { { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GeometryPush) } });

    m_ShadowLayout = makeLayout({ },
        { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush) } });

    m_LightingLayout = makeLayout({ m_LightingSetLayout }, { });

    m_PostLayout = makeLayout({ m_PostSetLayout },
        { { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) } });
}

// -----------------
// --- Pipelines ---
// -----------------

void Renderer::Impl::CreatePipelines()
{
    VkDevice device = m_Ctx.device;

    VkShaderModule fullscreenVert = LoadShader(device, "fullscreen.vert");
    VkShaderModule depthVert = LoadShader(device, "depth_prepass.vert");
    VkShaderModule shadowVert = LoadShader(device, "shadow.vert");
    VkShaderModule gbufferVert = LoadShader(device, "gbuffer.vert");
    VkShaderModule gbufferFrag = LoadShader(device, "gbuffer.frag");
    VkShaderModule lightingFrag = LoadShader(device, "lighting.frag");
    VkShaderModule postFrag = LoadShader(device, "post.frag");

    auto binding = VertexBinding();
    auto attrs = VertexAttributes();
    auto posAttr = PositionOnlyAttribute();

    // Depth prepass (depth-only, writes depth), position-only vertex input
    m_PrepassPipeline = PipelineBuilder()
        .SetShaders(depthVert, VK_NULL_HANDLE)
        .SetVertexInput(&binding, posAttr.data(), static_cast<uint32_t>(posAttr.size()))
        .SetLayout(m_PrepassLayout)
        .SetRenderPass(m_Targets.PrepassPass())
        .SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(true, true, VK_COMPARE_OP_LESS)
        .SetColorAttachmentCount(0)
        .Build(device);

    // Shadow (depth-only, with depth bias)
    m_ShadowPipeline = PipelineBuilder()
        .SetShaders(shadowVert, VK_NULL_HANDLE)
        .SetVertexInput(&binding, posAttr.data(), static_cast<uint32_t>(posAttr.size()))
        .SetLayout(m_ShadowLayout)
        .SetRenderPass(m_Targets.ShadowPass())
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(true, true, VK_COMPARE_OP_LESS)
        .SetDepthBias(true, m_Settings.shadowDepthBiasConstant, m_Settings.shadowDepthBiasSlope)
        .SetColorAttachmentCount(0)
        .Build(device);

    // G-buffer (loads prepass depth, EQUAL test, no depth write, 4 colour out)
    m_GBufferPipeline = PipelineBuilder()
        .SetShaders(gbufferVert, gbufferFrag)
        .SetVertexInput(&binding, attrs.data(), static_cast<uint32_t>(attrs.size()))
        .SetLayout(m_GBufferLayout)
        .SetRenderPass(m_Targets.GBufferPass())
        .SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(true, false, VK_COMPARE_OP_EQUAL)
        .SetColorAttachmentCount(RenderTargets::GBUFFER_COLOR_COUNT)
        .Build(device);

    // Lighting (fullscreen)
    m_LightingPipeline = PipelineBuilder()
        .SetShaders(fullscreenVert, lightingFrag)
        .SetLayout(m_LightingLayout)
        .SetRenderPass(m_Targets.LightingPass())
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(false, false, VK_COMPARE_OP_ALWAYS)
        .SetColorAttachmentCount(1)
        .Build(device);

    // Post (fullscreen)
    m_PostPipeline = PipelineBuilder()
        .SetShaders(fullscreenVert, postFrag)
        .SetLayout(m_PostLayout)
        .SetRenderPass(m_PostPass)
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(false, false, VK_COMPARE_OP_ALWAYS)
        .SetColorAttachmentCount(1)
        .Build(device);

    vkDestroyShaderModule(device, postFrag, nullptr);
    vkDestroyShaderModule(device, lightingFrag, nullptr);
    vkDestroyShaderModule(device, gbufferFrag, nullptr);
    vkDestroyShaderModule(device, gbufferVert, nullptr);
    vkDestroyShaderModule(device, shadowVert, nullptr);
    vkDestroyShaderModule(device, depthVert, nullptr);
    vkDestroyShaderModule(device, fullscreenVert, nullptr);
}

void Renderer::Impl::CreateUniformBuffers()
{
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        m_CameraUBOs[i] = m_Resources.CreateBuffer(sizeof(CameraUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

        m_LightingUBOs[i] = m_Resources.CreateBuffer(sizeof(LightingUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    }
}



// ----------------------
// --- IBL precompute ---
// ----------------------

void Renderer::Impl::RunIBLPrecompute()
{
    const std::string& envPath = m_Scene->GetEnvironmentMap();

    if (envPath.empty())
        return; // lighting falls back to constant ambient

    VkShaderModule cubeVert = LoadShader(m_Ctx.device, "cube.vert");
    VkShaderModule equirectFrag = LoadShader(m_Ctx.device, "equirect_to_cube.frag");
    VkShaderModule irradianceFrag = LoadShader(m_Ctx.device, "irradiance.frag");

    m_IBL.Generate(m_Ctx, m_CmdCtx, envPath, cubeVert, equirectFrag, irradianceFrag);

    vkDestroyShaderModule(m_Ctx.device, irradianceFrag, nullptr);
    vkDestroyShaderModule(m_Ctx.device, equirectFrag, nullptr);
    vkDestroyShaderModule(m_Ctx.device, cubeVert, nullptr);
}



// --------------------
// --- Scene upload ---
// --------------------

void Renderer::Impl::UploadMesh(Mesh& mesh)
{
    auto& vertices = mesh.GetVertices();
    auto& indices = mesh.GetIndices();

    VkDeviceSize vSize = sizeof(Vertex) * vertices.size();
    VkDeviceSize iSize = sizeof(uint32_t) * indices.size();

    // Staging buffers (host visible).
    AllocatedBuffer vStaging = m_Resources.CreateBuffer(vSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

    AllocatedBuffer iStaging = m_Resources.CreateBuffer(iSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

    void* data = nullptr;
    vmaMapMemory(m_Ctx.allocator, vStaging.allocation, &data);
    std::memcpy(data, vertices.data(), static_cast<size_t>(vSize));
    vmaUnmapMemory(m_Ctx.allocator, vStaging.allocation);

    vmaMapMemory(m_Ctx.allocator, iStaging.allocation, &data);
    std::memcpy(data, indices.data(), static_cast<size_t>(iSize));
    vmaUnmapMemory(m_Ctx.allocator, iStaging.allocation);

    // Device-local destination buffers (stored on the mesh)
    mesh.GetVertexBuffer() = m_Resources.CreateBuffer(vSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO, 0);

    mesh.GetIndexBuffer() = m_Resources.CreateBuffer(iSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO, 0);

    m_CmdCtx.ImmediateSubmit(m_Ctx, [&] (VkCommandBuffer cmd)
        {
            vkutil::CopyBuffer(cmd, vStaging.buffer, mesh.GetVertexBuffer().buffer, vSize);
            vkutil::CopyBuffer(cmd, iStaging.buffer, mesh.GetIndexBuffer().buffer, iSize);
        });

    m_Resources.DestroyBuffer(vStaging);
    m_Resources.DestroyBuffer(iStaging);

    mesh.SetIndexCount(static_cast<uint32_t>(indices.size()));
}

void Renderer::Impl::UploadMaterial(Material& material)
{
    auto uploadTexture = [&] (Texture& tex, AllocatedImage& outImage)
        {
            const uint32_t w = tex.GetWidth();
            const uint32_t h = tex.GetHeight();
            VkFormat format = tex.IsSrgb() ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

            // Mip support check
            VkFormatProperties props { };
            vkGetPhysicalDeviceFormatProperties(m_Ctx.physicalDevice, format, &props);
            bool canBlit =
                (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
                (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) &&
                (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

            uint32_t mipLevels = canBlit
                ? static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1
                : 1;

            VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;
            AllocatedBuffer staging = m_Resources.CreateBuffer(size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            void* data = nullptr;
            vmaMapMemory(m_Ctx.allocator, staging.allocation, &data);
            std::memcpy(data, tex.GetPixels().data(), static_cast<size_t>(size));
            vmaUnmapMemory(m_Ctx.allocator, staging.allocation);

            outImage = m_Resources.CreateImage(w, h, mipLevels, format,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT);

            m_CmdCtx.ImmediateSubmit(m_Ctx, [&] (VkCommandBuffer cmd)
                {
                    vkutil::TransitionImageLayout(cmd, outImage.image,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

                    vkutil::CopyBufferToImage(cmd, staging.buffer, outImage);

                    if (mipLevels > 1)
                    {
                        vkutil::GenerateMipmaps(cmd, outImage); // ends in SHADER_READ_ONLY
                    }
                    else
                    {
                        vkutil::TransitionImageLayout(cmd, outImage.image,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_ASPECT_COLOR_BIT, 1);
                    }
                });

            m_Resources.DestroyBuffer(staging);
            m_Resources.CreateImageView(outImage, VK_IMAGE_ASPECT_COLOR_BIT);
        };

    for (uint32_t i = 0; i < static_cast<uint32_t>(TextureSlot::Count); i++)
    {
        TextureSlot slot = static_cast<TextureSlot>(i);
        uploadTexture(material.GetTexture(slot), material.GetImage(slot));
    }
}

// ------------------------------
// --- Descriptor pool + sets ---
// ------------------------------

void Renderer::Impl::CreateDescriptorPoolAndSets()
{
    if (m_DescriptorPool)
    {
        vkDestroyDescriptorPool(m_Ctx.device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    const uint32_t numMaterials = static_cast<uint32_t>(m_Scene->GetMaterials().size());

    std::array<VkDescriptorPoolSize, 2> sizes { };

    sizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * FRAMES_IN_FLIGHT };

    sizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        5 * numMaterials + 7 * FRAMES_IN_FLIGHT + 1 };



    VkDescriptorPoolCreateInfo poolInfo { };

    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 2 * FRAMES_IN_FLIGHT + numMaterials + 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();

    if (vkCreateDescriptorPool(m_Ctx.device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");

    auto alloc = [&] (VkDescriptorSetLayout layout) -> VkDescriptorSet
        {
            VkDescriptorSetAllocateInfo ai { };

            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = m_DescriptorPool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &layout;

            VkDescriptorSet set { VK_NULL_HANDLE };

            if (vkAllocateDescriptorSets(m_Ctx.device, &ai, &set) != VK_SUCCESS)
                throw std::runtime_error("Failed to allocate descriptor set");

            return set;
        };

    auto writeUBO = [&] (VkDescriptorSet set, uint32_t binding, VkBuffer buffer, VkDeviceSize range)
        {
            VkDescriptorBufferInfo info { buffer, 0, range };
            VkWriteDescriptorSet w { };

            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo = &info;

            vkUpdateDescriptorSets(m_Ctx.device, 1, &w, 0, nullptr);
        };

    auto writeSampler = [&] (VkDescriptorSet set, uint32_t binding, VkSampler sampler,
        VkImageView view, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            VkDescriptorImageInfo info { sampler, view, layout };
            VkWriteDescriptorSet w { };

            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &info;

            vkUpdateDescriptorSets(m_Ctx.device, 1, &w, 0, nullptr);
        };

    // Camera sets
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        m_CameraSets[i] = alloc(m_CameraSetLayout);
        writeUBO(m_CameraSets[i], 0, m_CameraUBOs[i].buffer, sizeof(CameraUBO));
    }

    VkSampler iblSampler = m_IBL.IsValid() ? m_IBL.Sampler() : m_GBufferSampler;
    VkImageView iblView = m_IBL.IsValid() ? m_IBL.IrradianceView() : m_DummyCubeView;

    // Lighting sets.
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        m_LightingSets[i] = alloc(m_LightingSetLayout);

        writeSampler(m_LightingSets[i], 0, m_GBufferSampler, m_Targets.AlbedoView());
        writeSampler(m_LightingSets[i], 1, m_GBufferSampler, m_Targets.NormalView());
        writeSampler(m_LightingSets[i], 2, m_GBufferSampler, m_Targets.MetalRoughView());
        writeSampler(m_LightingSets[i], 3, m_GBufferSampler, m_Targets.EmissiveView());
        writeSampler(m_LightingSets[i], 4, m_GBufferSampler, m_Targets.DepthView(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        writeSampler(m_LightingSets[i], 5, m_ShadowSampler, m_Targets.ShadowView(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        writeSampler(m_LightingSets[i], 6, iblSampler, iblView);

        writeUBO(m_LightingSets[i], 7, m_LightingUBOs[i].buffer, sizeof(LightingUBO));
    }

    // Post set
    m_PostSet = alloc(m_PostSetLayout);
    writeSampler(m_PostSet, 0, m_GBufferSampler, m_Targets.HdrView());

    // Material sets
    m_MaterialSets.resize(numMaterials);
    for (uint32_t m = 0; m < numMaterials; m++)
    {
        Material& mat = *m_Scene->GetMaterials()[m];
        m_MaterialSets[m] = alloc(m_MaterialSetLayout);
        for (uint32_t s = 0; s < static_cast<uint32_t>(TextureSlot::Count); s++)
            writeSampler(m_MaterialSets[m], s, m_MaterialSampler,
                mat.GetImage(static_cast<TextureSlot>(s)).view);
    }
}

void Renderer::Impl::LoadScene(Scene& scene)
{
    m_Scene = &scene;

    RunIBLPrecompute();

    for (auto& mesh : scene.GetMeshes())
        UploadMesh(*mesh);

    for (auto& material : scene.GetMaterials())
        UploadMaterial(*material);

    CreateDescriptorPoolAndSets();
}



// -------------------------
// --- Per-frame updates ---
// -------------------------

glm::mat4 Renderer::Impl::ComputeLightViewProj(const DirectionalLight& light) const
{
    const glm::vec3 sceneMin = m_Scene->GetBoundsMin();
    const glm::vec3 sceneMax = m_Scene->GetBoundsMax();
    const glm::vec3 center = (sceneMin + sceneMax) * 0.5f;
    const float radius = glm::length(sceneMax - sceneMin) * 0.5f;

    glm::vec3 dir = glm::normalize(light.direction);
    glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);

    glm::vec3 eye = center - dir * (radius * 2.0f);
    glm::mat4 view = glm::lookAt(eye, center, up);

    const glm::vec3 c[8] = {
        { sceneMin.x, sceneMin.y, sceneMin.z },
        { sceneMax.x, sceneMin.y, sceneMin.z },
        { sceneMin.x, sceneMax.y, sceneMin.z },
        { sceneMax.x, sceneMax.y, sceneMin.z },
        { sceneMin.x, sceneMin.y, sceneMax.z },
        { sceneMax.x, sceneMin.y, sceneMax.z },
        { sceneMin.x, sceneMax.y, sceneMax.z },
        { sceneMax.x, sceneMax.y, sceneMax.z },
    };

    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : c)
    {
        glm::vec3 v = glm::vec3(view * glm::vec4(corner, 1.0f));
        mn = glm::min(mn, v);
        mx = glm::max(mx, v);
    }

    const float pad = radius * 0.02f + 0.01f;

    const float zNear = 0.0f;
    const float zFar = radius * 4.0f;

    glm::mat4 proj = glm::ortho(mn.x - pad, mx.x + pad,
        mn.y - pad, mx.y + pad,
        zNear, zFar);

    return proj * view;
}

void Renderer::Impl::UpdateCameraUBO(uint32_t frame, Camera& camera)
{
    CameraUBO ubo { };

    ubo.view = camera.GetViewMatrix();
    ubo.proj = camera.GetProjMatrix();

    void* data = nullptr;
    vmaMapMemory(m_Ctx.allocator, m_CameraUBOs[frame].allocation, &data);
    std::memcpy(data, &ubo, sizeof(ubo));
    vmaUnmapMemory(m_Ctx.allocator, m_CameraUBOs[frame].allocation);
}

void Renderer::Impl::UpdateLightingUBO(uint32_t frame, Camera& camera,
    const glm::mat4& lightViewProj)
{
    const SceneLighting& lighting = m_Scene->GetLighting();
    const DirectionalLight& dir = lighting.GetDirectional();
    const auto& points = lighting.GetPointLights();

    LightingUBO ubo { };

    ubo.invViewProj = glm::inverse(camera.GetProjMatrix() * camera.GetViewMatrix());
    ubo.lightViewProj = lightViewProj;
    ubo.camPos = glm::vec4(camera.GetTransform().GetPosition(), 1.0f);
    ubo.dirDirection = glm::vec4(glm::normalize(dir.direction), 0.0f);
    ubo.dirColor = glm::vec4(dir.color, dir.illuminanceLux);

    bool hasIBL = m_IBL.IsValid();
    ubo.ambient = glm::vec4(m_Settings.ambientColor, hasIBL ? 1.0f : 0.0f);

    uint32_t numPoints = static_cast<uint32_t>(std::min<size_t>(points.size(), MAX_POINT_LIGHTS));
    ubo.params = glm::vec4(static_cast<float>(numPoints),
        m_Settings.shadowBias,
        m_Settings.iblIntensity,
        0.0f);

    for (uint32_t i = 0; i < numPoints; i++)
    {
        ubo.points[i].position = glm::vec4(points[i].position, points[i].range);
        ubo.points[i].color = glm::vec4(points[i].color, points[i].intensityCandela);
    }

    void* data = nullptr;
    vmaMapMemory(m_Ctx.allocator, m_LightingUBOs[frame].allocation, &data);
    std::memcpy(data, &ubo, sizeof(ubo));
    vmaUnmapMemory(m_Ctx.allocator, m_LightingUBOs[frame].allocation);
}



// ----------------------------------------------------------------------------
// --- Command recording (shadow -> prepass -> gbuffer -> lighting -> post) ---
// ----------------------------------------------------------------------------

void Renderer::Impl::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, Camera& camera)
{
    VkCommandBufferBeginInfo begin { };
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin command buffer");

    auto& meshes = m_Scene->GetMeshes();
    const VkExtent2D extent = m_Swapchain.swapchainExtent;
    const VkExtent2D shadowExtent = m_Targets.ShadowExtent();

    auto setViewportScissor = [&] (VkExtent2D e)
        {
            VkViewport vp { 0.0f, 0.0f, static_cast<float>(e.width), static_cast<float>(e.height),
                0.0f, 1.0f };
            VkRect2D sc { { 0, 0 }, e };
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
        };

    auto bindMeshBuffers = [&] (Mesh& mesh)
        {
            VkBuffer vb = mesh.GetVertexBuffer().buffer;
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
            vkCmdBindIndexBuffer(cmd, mesh.GetIndexBuffer().buffer, 0, VK_INDEX_TYPE_UINT32);
        };

    const glm::mat4 lightVP = ComputeLightViewProj(m_Scene->GetLighting().GetDirectional());

    // Shadow pass
    {
        VkClearValue clear { };
        clear.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_Targets.ShadowPass();
        rp.framebuffer = m_Targets.ShadowFramebuffer();
        rp.renderArea.extent = shadowExtent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        setViewportScissor(shadowExtent);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ShadowPipeline);

        for (auto& mesh : meshes)
        {
            ShadowPush push { lightVP, mesh->GetTransform().GetModelMatrix() };
            vkCmdPushConstants(cmd, m_ShadowLayout, VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(push), &push);
            bindMeshBuffers(*mesh);
            vkCmdDrawIndexed(cmd, mesh->GetIndexCount(), 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(cmd);
    }

    // Depth prepass
    {
        VkClearValue clear { };
        clear.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_Targets.PrepassPass();
        rp.framebuffer = m_Targets.PrepassFramebuffer();
        rp.renderArea.extent = extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        setViewportScissor(extent);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PrepassPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PrepassLayout,
            0, 1, &m_CameraSets[m_CurrentFrame], 0, nullptr);

        for (auto& mesh : meshes)
        {
            DepthPush push { mesh->GetTransform().GetModelMatrix() };
            vkCmdPushConstants(cmd, m_PrepassLayout, VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(push), &push);
            bindMeshBuffers(*mesh);
            vkCmdDrawIndexed(cmd, mesh->GetIndexCount(), 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(cmd);
    }

    // G-buffer pass
    {
        std::array<VkClearValue, RenderTargets::GBUFFER_COLOR_COUNT + 1> clears { };
        for (uint32_t i = 0; i < RenderTargets::GBUFFER_COLOR_COUNT; i++)
            clears[i].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clears[RenderTargets::GBUFFER_COLOR_COUNT].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_Targets.GBufferPass();
        rp.framebuffer = m_Targets.GBufferFramebuffer();
        rp.renderArea.extent = extent;
        rp.clearValueCount = static_cast<uint32_t>(clears.size());
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        setViewportScissor(extent);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GBufferPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GBufferLayout,
            0, 1, &m_CameraSets[m_CurrentFrame], 0, nullptr);

        for (auto& mesh : meshes)
        {
            size_t matId = mesh->GetMaterialId();
            if (matId >= m_MaterialSets.size()) matId = 0;

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GBufferLayout,
                1, 1, &m_MaterialSets[matId], 0, nullptr);

            Material& mat = *m_Scene->GetMaterials()[matId];
            const MaterialFactors& f = mat.GetFactors();

            GeometryPush push { };
            push.model = mesh->GetTransform().GetModelMatrix();
            push.baseColorFactor = f.baseColorFactor;
            push.emissiveFactor = f.emissiveFactor;
            push.metallicFactor = f.metallicFactor;
            push.roughnessFactor = f.roughnessFactor;
            push.normalScale = f.normalScale;
            push.occlusionStrength = f.occlusionStrength;

            vkCmdPushConstants(cmd, m_GBufferLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(push), &push);

            bindMeshBuffers(*mesh);
            vkCmdDrawIndexed(cmd, mesh->GetIndexCount(), 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(cmd);
    }

    // Lighting pass (fullscreen)
    {
        VkClearValue clear { };
        clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

        VkRenderPassBeginInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_Targets.LightingPass();
        rp.framebuffer = m_Targets.LightingFramebuffer();
        rp.renderArea.extent = extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        setViewportScissor(extent);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LightingPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LightingLayout,
            0, 1, &m_LightingSets[m_CurrentFrame], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // Post pass (fullscreen -> swapchain)
    {
        VkClearValue clear { };
        clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

        VkRenderPassBeginInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_PostPass;
        rp.framebuffer = m_PostFramebuffers[imageIndex];
        rp.renderArea.extent = extent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

        setViewportScissor(extent);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PostPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PostLayout,
            0, 1, &m_PostSet, 0, nullptr);

        float exposure = camera.GetExposure();
        vkCmdPushConstants(cmd, m_PostLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(float), &exposure);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer");
}



// ---------------------------------------------------------------------
//  Draw / present
// ---------------------------------------------------------------------

void Renderer::Impl::RecreateSwapchain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(m_Window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_Ctx.device);

    DestroyPostFramebuffers();
    m_Targets.DestroySizedResources(m_Ctx, m_Resources);
    m_Swapchain.Destroy(m_Ctx, m_Resources);

    m_Swapchain.CreateSwapchainOnly(m_Ctx, m_Window);
    m_Targets.CreateSizedResources(m_Ctx, m_Resources, m_Swapchain.swapchainExtent);
    CreatePostFramebuffers();

    // Lighting/post sets reference the offscreen views, which were recreated.
    if (m_Scene)
        CreateDescriptorPoolAndSets();
}

void Renderer::Impl::DrawFrame(Camera& camera)
{
    if (!m_Scene || m_Scene->GetMeshes().empty())
        return;

    vkWaitForFences(m_Ctx.device, 1, &m_Sync.inFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(m_Ctx.device, m_Swapchain.swapchain, UINT64_MAX,
        m_Sync.imageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &imageIndex);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RecreateSwapchain();
        return;
    }
    else if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swapchain image");
    }

    const glm::mat4 lightVP = ComputeLightViewProj(m_Scene->GetLighting().GetDirectional());
    UpdateCameraUBO(m_CurrentFrame, camera);
    UpdateLightingUBO(m_CurrentFrame, camera, lightVP);

    vkResetFences(m_Ctx.device, 1, &m_Sync.inFlightFences[m_CurrentFrame]);

    VkCommandBuffer cmd = m_CmdCtx.commandBuffers[m_CurrentFrame];
    vkResetCommandBuffer(cmd, 0);
    RecordCommandBuffer(cmd, imageIndex, camera);

    VkSemaphore waitSemaphores[] = { m_Sync.imageAvailableSemaphores[m_CurrentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { m_Sync.renderFinishedSemaphores[imageIndex] };

    VkSubmitInfo submit { };
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = waitSemaphores;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_Ctx.graphicsQueue, 1, &submit,
        m_Sync.inFlightFences[m_CurrentFrame]) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command buffer");

    VkPresentInfoKHR present { };
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = signalSemaphores;
    present.swapchainCount = 1;
    present.pSwapchains = &m_Swapchain.swapchain;
    present.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(m_Ctx.presentQueue, &present);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_FramebufferResized)
    {
        m_FramebufferResized = false;
        RecreateSwapchain();
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image");
    }

    m_CurrentFrame = (m_CurrentFrame + 1) % FRAMES_IN_FLIGHT;
}



// --------------------------
// --- Renderer interface ---
// --------------------------

Renderer::Renderer(GLFWwindow* window, const RenderSettings& settings)
{
    m_Impl = std::make_unique<Impl>(window, settings);
}

Renderer::~Renderer() = default;

void Renderer::LoadScene(Scene& scene) { m_Impl->LoadScene(scene); }
void Renderer::DrawFrame(Camera& camera) { m_Impl->DrawFrame(camera); }
void Renderer::DeviceWaitIdle() { m_Impl->DeviceWaitIdle(); }
RenderSettings& Renderer::GetSettings() { return m_Impl->GetSettings(); }