#include "render_targets.h"

#include "vulkan_context.h"
#include "resource_manager.h"

#include <array>
#include <stdexcept>

// Render pass creation (size-independent)

void RenderTargets::CreateRenderPasses(const VulkanContext& ctx)
{
    m_DepthFormat = ctx.FindDepthFormat();

    // Two external dependencies per pass: one making prior attachment writes
    // available before this pass, and one making this pass's writes available
    // to subsequent fragment-shader sampling (the deferred reads)
    auto makeDependencies = [] () -> std::array<VkSubpassDependency, 2>
        {
            std::array<VkSubpassDependency, 2> deps { };

            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_SHADER_READ_BIT;
            deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            return deps;
        };

    // Shadow pass: 1 depth attachment, ends sampled
    {
        VkAttachmentDescription depth { };
        depth.format = SHADOW_FORMAT;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass { };
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        auto deps = makeDependencies();

        VkRenderPassCreateInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &depth;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = static_cast<uint32_t>(deps.size());
        rp.pDependencies = deps.data();

        if (vkCreateRenderPass(ctx.device, &rp, nullptr, &m_ShadowPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shadow render pass");
    }

    // Depth prepass: 1 depth attachment, kept as attachment
    {
        VkAttachmentDescription depth { };
        depth.format = m_DepthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass { };
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depthRef;

        auto deps = makeDependencies();

        VkRenderPassCreateInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &depth;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = static_cast<uint32_t>(deps.size());
        rp.pDependencies = deps.data();

        if (vkCreateRenderPass(ctx.device, &rp, nullptr, &m_PrepassPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create prepass render pass");
    }

    // G-buffer pass: 4 colour + 1 depth (depth loaded, EQUAL)
    {
        std::array<VkAttachmentDescription, GBUFFER_COLOR_COUNT + 1> attachments { };
        std::array<VkFormat, GBUFFER_COLOR_COUNT> colorFormats = {
            ALBEDO_FORMAT, NORMAL_FORMAT, METALROUGH_FORMAT, EMISSIVE_FORMAT
        };

        for (uint32_t i = 0; i < GBUFFER_COLOR_COUNT; i++)
        {
            attachments[i].format = colorFormats[i];
            attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        // Depth: loaded from the prepass, read-only (EQUAL), ends sampled
        VkAttachmentDescription& depth = attachments[GBUFFER_COLOR_COUNT];
        depth.format = m_DepthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        std::array<VkAttachmentReference, GBUFFER_COLOR_COUNT> colorRefs { };
        for (uint32_t i = 0; i < GBUFFER_COLOR_COUNT; i++)
            colorRefs[i] = { i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkAttachmentReference depthRef {
            GBUFFER_COLOR_COUNT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass { };
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = GBUFFER_COLOR_COUNT;
        subpass.pColorAttachments = colorRefs.data();
        subpass.pDepthStencilAttachment = &depthRef;

        auto deps = makeDependencies();

        VkRenderPassCreateInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = static_cast<uint32_t>(attachments.size());
        rp.pAttachments = attachments.data();
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = static_cast<uint32_t>(deps.size());
        rp.pDependencies = deps.data();

        if (vkCreateRenderPass(ctx.device, &rp, nullptr, &m_GBufferPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create G-buffer render pass");
    }

    // Lighting pass: 1 HDR colour, ends sampled
    {
        VkAttachmentDescription color { };
        color.format = HDR_FORMAT;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass { };
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        auto deps = makeDependencies();

        VkRenderPassCreateInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = static_cast<uint32_t>(deps.size());
        rp.pDependencies = deps.data();

        if (vkCreateRenderPass(ctx.device, &rp, nullptr, &m_LightingPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create lighting render pass");
    }
}

void RenderTargets::DestroyRenderPasses(const VulkanContext& ctx)
{
    if (m_LightingPass) vkDestroyRenderPass(ctx.device, m_LightingPass, nullptr);
    if (m_GBufferPass)  vkDestroyRenderPass(ctx.device, m_GBufferPass, nullptr);
    if (m_PrepassPass)  vkDestroyRenderPass(ctx.device, m_PrepassPass, nullptr);
    if (m_ShadowPass)   vkDestroyRenderPass(ctx.device, m_ShadowPass, nullptr);

    m_LightingPass = m_GBufferPass = m_PrepassPass = m_ShadowPass = VK_NULL_HANDLE;
}

// Sized resources (images + framebuffers)

void RenderTargets::CreateSizedResources(const VulkanContext& ctx,
    ResourceManager& resources, VkExtent2D extent)
{
    m_Extent = extent;

    const VkImageUsageFlags colorUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags depthUsage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // Shadow map
    m_ShadowDepth = resources.CreateImage(m_ShadowResolution, m_ShadowResolution, 1,
        SHADOW_FORMAT, depthUsage);
    resources.CreateImageView(m_ShadowDepth, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Scene depth (shared by prepass + G-buffer)
    m_Depth = resources.CreateImage(extent.width, extent.height, 1,
        m_DepthFormat, depthUsage);
    resources.CreateImageView(m_Depth, VK_IMAGE_ASPECT_DEPTH_BIT);

    // G-buffer colour targets
    m_Albedo = resources.CreateImage(extent.width, extent.height, 1, ALBEDO_FORMAT, colorUsage);
    resources.CreateImageView(m_Albedo, VK_IMAGE_ASPECT_COLOR_BIT);

    m_Normal = resources.CreateImage(extent.width, extent.height, 1, NORMAL_FORMAT, colorUsage);
    resources.CreateImageView(m_Normal, VK_IMAGE_ASPECT_COLOR_BIT);

    m_MetalRough = resources.CreateImage(extent.width, extent.height, 1, METALROUGH_FORMAT, colorUsage);
    resources.CreateImageView(m_MetalRough, VK_IMAGE_ASPECT_COLOR_BIT);

    m_Emissive = resources.CreateImage(extent.width, extent.height, 1, EMISSIVE_FORMAT, colorUsage);
    resources.CreateImageView(m_Emissive, VK_IMAGE_ASPECT_COLOR_BIT);

    // HDR lighting target
    m_Hdr = resources.CreateImage(extent.width, extent.height, 1, HDR_FORMAT, colorUsage);
    resources.CreateImageView(m_Hdr, VK_IMAGE_ASPECT_COLOR_BIT);

    auto makeFramebuffer = [&] (VkRenderPass rp, const std::vector<VkImageView>& views,
        VkExtent2D ext) -> VkFramebuffer
        {
            VkFramebufferCreateInfo fb { };
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = rp;
            fb.attachmentCount = static_cast<uint32_t>(views.size());
            fb.pAttachments = views.data();
            fb.width = ext.width;
            fb.height = ext.height;
            fb.layers = 1;

            VkFramebuffer handle { VK_NULL_HANDLE };
            if (vkCreateFramebuffer(ctx.device, &fb, nullptr, &handle) != VK_SUCCESS)
                throw std::runtime_error("Failed to create framebuffer");
            return handle;
        };

    m_ShadowFB = makeFramebuffer(m_ShadowPass, { m_ShadowDepth.view },
        { m_ShadowResolution, m_ShadowResolution });

    m_PrepassFB = makeFramebuffer(m_PrepassPass, { m_Depth.view }, extent);

    m_GBufferFB = makeFramebuffer(m_GBufferPass,
        { m_Albedo.view, m_Normal.view, m_MetalRough.view, m_Emissive.view, m_Depth.view },
        extent);

    m_LightingFB = makeFramebuffer(m_LightingPass, { m_Hdr.view }, extent);
}

void RenderTargets::DestroySizedResources(const VulkanContext& ctx, ResourceManager& resources)
{
    if (m_LightingFB) vkDestroyFramebuffer(ctx.device, m_LightingFB, nullptr);
    if (m_GBufferFB) vkDestroyFramebuffer(ctx.device, m_GBufferFB, nullptr);
    if (m_PrepassFB) vkDestroyFramebuffer(ctx.device, m_PrepassFB, nullptr);
    if (m_ShadowFB) vkDestroyFramebuffer(ctx.device, m_ShadowFB, nullptr);
    m_LightingFB = m_GBufferFB = m_PrepassFB = m_ShadowFB = VK_NULL_HANDLE;

    resources.DestroyImage(m_Hdr);
    resources.DestroyImage(m_Emissive);
    resources.DestroyImage(m_MetalRough);
    resources.DestroyImage(m_Normal);
    resources.DestroyImage(m_Albedo);
    resources.DestroyImage(m_Depth);
    resources.DestroyImage(m_ShadowDepth);
}