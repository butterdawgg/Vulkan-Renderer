#include "ibl.h"

#include "vulkan_context.h"
#include "command_context.h"
#include "pipeline_builder.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image.h> // declarations only; STB_IMAGE_IMPLEMENTATION lives in gltf_loader.cpp

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    // The 6 view matrices that look down each cube face, plus a 90-degree proj
    std::array<glm::mat4, 6> CaptureViewProjections()
    {
        glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

        const glm::vec3 o(0.0f);

        std::array<glm::mat4, 6> views = {
            glm::lookAt(o, glm::vec3( 1, 0, 0), glm::vec3(0, -1,  0)), // +X
            glm::lookAt(o, glm::vec3(-1, 0, 0), glm::vec3(0, -1,  0)), // -X
            glm::lookAt(o, glm::vec3( 0, 1, 0), glm::vec3(0,  0,  1)), // +Y
            glm::lookAt(o, glm::vec3( 0,-1, 0), glm::vec3(0,  0, -1)), // -Y
            glm::lookAt(o, glm::vec3( 0, 0, 1), glm::vec3(0, -1,  0)), // +Z
            glm::lookAt(o, glm::vec3( 0, 0,-1), glm::vec3(0, -1,  0))  // -Z
        };

        std::array<glm::mat4, 6> result;

        for (int i = 0; i < 6; i++)
            result[i] = proj * views[i];

        return result;
    }

    VkImageView CreateView(VkDevice device, VkImage image, VkImageViewType type,
        VkFormat format, uint32_t baseLayer, uint32_t layerCount)
    {
        VkImageViewCreateInfo info { };

        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = type;
        info.format = format;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = baseLayer;
        info.subresourceRange.layerCount = layerCount;

        VkImageView view { VK_NULL_HANDLE };

        if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS)
            throw std::runtime_error("IBL: failed to create image view");

        return view;
    }
}

bool IBL::Generate(const VulkanContext& ctx, const CommandContext& cmdCtx,
    const std::string& equirectPath,
    VkShaderModule cubeVert, VkShaderModule equirectFrag, VkShaderModule irradianceFrag)
{
    VkDevice device = ctx.device;

    // Load the HDR equirectangular image
    int width, height, channels;
    float* pixels = stbi_loadf(equirectPath.c_str(), &width, &height, &channels, 4);
    if (!pixels)
        return false; // renderer will fall back to constant ambient

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4 * sizeof(float);

    // Staging buffer for the equirect upload
    VkBuffer staging { VK_NULL_HANDLE };
    VmaAllocation stagingAlloc { VK_NULL_HANDLE };

    {
        VkBufferCreateInfo bi { };
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = imageSize;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai { };
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo info { };
        vmaCreateBuffer(ctx.allocator, &bi, &ai, &staging, &stagingAlloc, &info);
        std::memcpy(info.pMappedData, pixels, static_cast<size_t>(imageSize));
    }

    stbi_image_free(pixels);

    // Equirect image (float)
    VkImage equirect { VK_NULL_HANDLE };
    VmaAllocation equirectAlloc { VK_NULL_HANDLE };

    {
        VkImageCreateInfo ii { };
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        ii.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai { };
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage(ctx.allocator, &ii, &ai, &equirect, &equirectAlloc, nullptr);
    }

    VkImageView equirectView = CreateView(device, equirect,
        VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, 0, 1);

    // Upload: transition -> copy -> transition to shader read
    cmdCtx.ImmediateSubmit(ctx, [&](VkCommandBuffer cmd)
    {
        VkImageMemoryBarrier toDst { };
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDst.image = equirect;
        toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy copy { };
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        vkCmdCopyBufferToImage(cmd, staging, equirect,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier toRead = toDst;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
    });

    vmaDestroyBuffer(ctx.allocator, staging, stagingAlloc);

    // Shared sampler for sampling equirect + env cube during convolution
    VkSampler linearSampler { VK_NULL_HANDLE };
    {
        VkSamplerCreateInfo si { };
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = VK_LOD_CLAMP_NONE;
        vkCreateSampler(device, &si, nullptr, &linearSampler);
    }

    // Cube render pass (1 colour attachment, ends sampled)
    VkRenderPass cubeRenderPass { VK_NULL_HANDLE };
    {
        VkAttachmentDescription color { };
        color.format = CUBE_FORMAT;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass { };
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

        VkRenderPassCreateInfo rp { };
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &color;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        vkCreateRenderPass(device, &rp, nullptr, &cubeRenderPass);
    }

    // Descriptor set layout (single combined image sampler)
    VkDescriptorSetLayout setLayout { VK_NULL_HANDLE };
    {
        VkDescriptorSetLayoutBinding b { };
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci { };
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings = &b;
        vkCreateDescriptorSetLayout(device, &ci, nullptr, &setLayout);
    }

    VkDescriptorPool pool { VK_NULL_HANDLE };
    {
        VkDescriptorPoolSize size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 };
        VkDescriptorPoolCreateInfo ci { };
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.maxSets = 2;
        ci.poolSizeCount = 1;
        ci.pPoolSizes = &size;
        vkCreateDescriptorPool(device, &ci, nullptr, &pool);
    }

    // Pipeline layout: 1 set + push constant mat4 viewProj (vertex)
    VkPipelineLayout pipelineLayout { VK_NULL_HANDLE };
    {
        VkPushConstantRange pc { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) };
        VkPipelineLayoutCreateInfo ci { };
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &setLayout;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &pc;
        vkCreatePipelineLayout(device, &ci, nullptr, &pipelineLayout);
    }

    auto allocateSet = [&](VkImageView view) -> VkDescriptorSet
    {
        VkDescriptorSetAllocateInfo ai { };
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout;

        VkDescriptorSet set { VK_NULL_HANDLE };
        vkAllocateDescriptorSets(device, &ai, &set);

        VkDescriptorImageInfo image { linearSampler, view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet write { };
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        return set;
    };

    // Pipelines (no vertex input; cube positions come from gl_VertexIndex)
    VkPipeline equirectPipeline = PipelineBuilder()
        .SetShaders(cubeVert, equirectFrag)
        .SetLayout(pipelineLayout)
        .SetRenderPass(cubeRenderPass)
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(false, false, VK_COMPARE_OP_ALWAYS)
        .SetColorAttachmentCount(1)
        .Build(device);

    VkPipeline irradiancePipeline = PipelineBuilder()
        .SetShaders(cubeVert, irradianceFrag)
        .SetLayout(pipelineLayout)
        .SetRenderPass(cubeRenderPass)
        .SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .SetDepth(false, false, VK_COMPARE_OP_ALWAYS)
        .SetColorAttachmentCount(1)
        .Build(device);

    auto captures = CaptureViewProjections();

    // Helper: render all 6 faces of a cubemap of the given size, using the
    // given pipeline + source descriptor set
    auto renderCube = [&](VkImage cubeImage, uint32_t size,
        const std::array<VkImageView, 6>& faceViews,
        VkPipeline pipeline, VkDescriptorSet srcSet)
    {
        std::array<VkFramebuffer, 6> fbs { };
        for (int face = 0; face < 6; face++)
        {
            VkFramebufferCreateInfo fb { };
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = cubeRenderPass;
            fb.attachmentCount = 1;
            fb.pAttachments = &faceViews[face];
            fb.width = size;
            fb.height = size;
            fb.layers = 1;
            vkCreateFramebuffer(device, &fb, nullptr, &fbs[face]);
        }

        cmdCtx.ImmediateSubmit(ctx, [&](VkCommandBuffer cmd)
        {
            for (int face = 0; face < 6; face++)
            {
                VkClearValue clear { };
                clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

                VkRenderPassBeginInfo bi { };
                bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                bi.renderPass = cubeRenderPass;
                bi.framebuffer = fbs[face];
                bi.renderArea.extent = { size, size };
                bi.clearValueCount = 1;
                bi.pClearValues = &clear;
                vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport vp { 0.0f, 0.0f, static_cast<float>(size),
                    static_cast<float>(size), 0.0f, 1.0f };
                VkRect2D sc { { 0, 0 }, { size, size } };
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipelineLayout, 0, 1, &srcSet, 0, nullptr);
                vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                    0, sizeof(glm::mat4), &captures[face]);

                vkCmdDraw(cmd, 36, 1, 0, 0);
                vkCmdEndRenderPass(cmd);
            }
        });

        for (auto fb : fbs)
            vkDestroyFramebuffer(device, fb, nullptr);
    };

    // ---- 2. Environment cubemap ----
    VkImage envCube { VK_NULL_HANDLE };
    VmaAllocation envAlloc { VK_NULL_HANDLE };
    {
        VkImageCreateInfo ii { };
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = CUBE_FORMAT;
        ii.extent = { ENV_SIZE, ENV_SIZE, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 6;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai { };
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage(ctx.allocator, &ii, &ai, &envCube, &envAlloc, nullptr);
    }

    std::array<VkImageView, 6> envFaceViews { };
    for (int i = 0; i < 6; i++)
        envFaceViews[i] = CreateView(device, envCube, VK_IMAGE_VIEW_TYPE_2D, CUBE_FORMAT, i, 1);
    VkImageView envCubeView = CreateView(device, envCube, VK_IMAGE_VIEW_TYPE_CUBE, CUBE_FORMAT, 0, 6);

    VkDescriptorSet equirectSet = allocateSet(equirectView);
    renderCube(envCube, ENV_SIZE, envFaceViews, equirectPipeline, equirectSet);

    // ---- 3. Irradiance cubemap (persistent output) ----
    {
        VkImageCreateInfo ii { };
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = CUBE_FORMAT;
        ii.extent = { IRRADIANCE_SIZE, IRRADIANCE_SIZE, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 6;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai { };
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        vmaCreateImage(ctx.allocator, &ii, &ai, &m_IrradianceImage, &m_IrradianceAlloc, nullptr);
    }

    std::array<VkImageView, 6> irrFaceViews { };
    for (int i = 0; i < 6; i++)
        irrFaceViews[i] = CreateView(device, m_IrradianceImage, VK_IMAGE_VIEW_TYPE_2D, CUBE_FORMAT, i, 1);
    m_IrradianceView = CreateView(device, m_IrradianceImage, VK_IMAGE_VIEW_TYPE_CUBE, CUBE_FORMAT, 0, 6);

    VkDescriptorSet envSet = allocateSet(envCubeView);
    renderCube(m_IrradianceImage, IRRADIANCE_SIZE, irrFaceViews, irradiancePipeline, envSet);

    // Persistent sampler for sampling the irradiance cube in the lighting pass
    {
        VkSamplerCreateInfo si { };
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = VK_LOD_CLAMP_NONE;
        vkCreateSampler(device, &si, nullptr, &m_Sampler);
    }

    // Destroy all transient resources
    for (auto v : irrFaceViews) vkDestroyImageView(device, v, nullptr);
    vkDestroyImageView(device, envCubeView, nullptr);
    for (auto v : envFaceViews) vkDestroyImageView(device, v, nullptr);
    vmaDestroyImage(ctx.allocator, envCube, envAlloc);

    vkDestroyPipeline(device, irradiancePipeline, nullptr);
    vkDestroyPipeline(device, equirectPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    vkDestroyRenderPass(device, cubeRenderPass, nullptr);
    vkDestroySampler(device, linearSampler, nullptr);
    vkDestroyImageView(device, equirectView, nullptr);
    vmaDestroyImage(ctx.allocator, equirect, equirectAlloc);

    m_Valid = true;
    return true;
}

void IBL::Destroy(const VulkanContext& ctx)
{
    if (m_Sampler)         vkDestroySampler(ctx.device, m_Sampler, nullptr);
    if (m_IrradianceView)  vkDestroyImageView(ctx.device, m_IrradianceView, nullptr);
    if (m_IrradianceImage) vmaDestroyImage(ctx.allocator, m_IrradianceImage, m_IrradianceAlloc);

    m_Sampler = VK_NULL_HANDLE;
    m_IrradianceView = VK_NULL_HANDLE;
    m_IrradianceImage = VK_NULL_HANDLE;
    m_Valid = false;
}