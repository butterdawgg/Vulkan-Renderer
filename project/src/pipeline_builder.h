#ifndef PIPELINE_BUILDER_H
#define PIPELINE_BUILDER_H

// Small helper that collects graphics-pipeline state and builds a VkPipeline

#include <vulkan/vulkan.h>

#include <vector>

class PipelineBuilder
{
    public:

    PipelineBuilder& SetShaders(VkShaderModule vert, VkShaderModule frag);
    PipelineBuilder& SetVertexInput(
        const VkVertexInputBindingDescription* binding,
        const VkVertexInputAttributeDescription* attributes,
        uint32_t attributeCount);
    PipelineBuilder& SetLayout(VkPipelineLayout layout);
    PipelineBuilder& SetRenderPass(VkRenderPass renderPass, uint32_t subpass = 0);

    PipelineBuilder& SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
    PipelineBuilder& SetDepth(bool testEnable, bool writeEnable, VkCompareOp compareOp);
    PipelineBuilder& SetDepthBias(bool enable, float constantFactor = 0.0f, float slopeFactor = 0.0f);

    // Number of (opaque, non-blended) colour attachments the pipeline writes
    PipelineBuilder& SetColorAttachmentCount(uint32_t count);

    VkPipeline Build(VkDevice device);

    private:

    VkShaderModule m_Vert { VK_NULL_HANDLE };
    VkShaderModule m_Frag { VK_NULL_HANDLE };

    const VkVertexInputBindingDescription* m_VertexBinding { nullptr };
    const VkVertexInputAttributeDescription* m_VertexAttributes { nullptr };
    uint32_t m_VertexAttributeCount { 0 };

    VkPipelineLayout m_Layout { VK_NULL_HANDLE };
    VkRenderPass m_RenderPass { VK_NULL_HANDLE };
    uint32_t m_Subpass { 0 };

    VkCullModeFlags m_CullMode { VK_CULL_MODE_BACK_BIT };
    VkFrontFace m_FrontFace { VK_FRONT_FACE_COUNTER_CLOCKWISE };

    bool m_DepthTest { true };
    bool m_DepthWrite { true };
    VkCompareOp m_DepthCompare { VK_COMPARE_OP_LESS };

    bool m_DepthBiasEnable { false };
    float m_DepthBiasConstant { 0.0f };
    float m_DepthBiasSlope { 0.0f };

    uint32_t m_ColorAttachmentCount { 1 };
};

#endif // !PIPELINE_BUILDER_H