#include "pipeline_builder.h"

#include <array>
#include <stdexcept>

PipelineBuilder& PipelineBuilder::SetShaders(VkShaderModule vert, VkShaderModule frag)
{
    m_Vert = vert;
    m_Frag = frag;
    return *this;
}

PipelineBuilder& PipelineBuilder::SetVertexInput(
    const VkVertexInputBindingDescription* binding,
    const VkVertexInputAttributeDescription* attributes,
    uint32_t attributeCount)
{
    m_VertexBinding = binding;
    m_VertexAttributes = attributes;
    m_VertexAttributeCount = attributeCount;
    return *this;
}

PipelineBuilder& PipelineBuilder::SetLayout(VkPipelineLayout layout)
{
    m_Layout = layout;
    return *this;
}

PipelineBuilder& PipelineBuilder::SetRenderPass(VkRenderPass renderPass, uint32_t subpass)
{
    m_RenderPass = renderPass;
    m_Subpass = subpass;
    return *this;
}

PipelineBuilder& PipelineBuilder::SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
    m_CullMode = cullMode;
    m_FrontFace = frontFace;
    return *this;
}

PipelineBuilder& PipelineBuilder::SetDepth(bool testEnable, bool writeEnable, VkCompareOp compareOp)
{
    m_DepthTest = testEnable;
    m_DepthWrite = writeEnable;
    m_DepthCompare = compareOp;
    return *this;
}

PipelineBuilder& PipelineBuilder::SetDepthBias(bool enable, float constantFactor, float slopeFactor)
{
    m_DepthBiasEnable = enable;
    m_DepthBiasConstant = constantFactor;
    m_DepthBiasSlope = slopeFactor;
    return *this;
}

PipelineBuilder& PipelineBuilder::SetColorAttachmentCount(uint32_t count)
{
    m_ColorAttachmentCount = count;
    return *this;
}

VkPipeline PipelineBuilder::Build(VkDevice device)
{
    std::vector<VkPipelineShaderStageCreateInfo> stages;

    VkPipelineShaderStageCreateInfo vertStage { };
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = m_Vert;
    vertStage.pName = "main";
    stages.push_back(vertStage);

    // Depth-only pipelines (prepass, shadow) have no fragment shader.
    if (m_Frag != VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo fragStage { };
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = m_Frag;
        fragStage.pName = "main";
        stages.push_back(fragStage);
    }

    VkPipelineVertexInputStateCreateInfo vertexInput { };
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (m_VertexBinding)
    {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = m_VertexBinding;
        vertexInput.vertexAttributeDescriptionCount = m_VertexAttributeCount;
        vertexInput.pVertexAttributeDescriptions = m_VertexAttributes;
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly { };
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState { };
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer { };
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = m_CullMode;
    rasterizer.frontFace = m_FrontFace;
    rasterizer.depthBiasEnable = m_DepthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = m_DepthBiasConstant;
    rasterizer.depthBiasSlopeFactor = m_DepthBiasSlope;

    VkPipelineMultisampleStateCreateInfo multisampling { };
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil { };
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_DepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_DepthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = m_DepthCompare;

    // Opaque colour attachments (no blending).
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        m_ColorAttachmentCount);
    for (auto& a : blendAttachments)
    {
        a = { };
        a.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        a.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending { };
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlending.pAttachments = blendAttachments.empty() ? nullptr : blendAttachments.data();

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState { };
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo { };
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_Layout;
    pipelineInfo.renderPass = m_RenderPass;
    pipelineInfo.subpass = m_Subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline { VK_NULL_HANDLE };
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
        &pipeline) != VK_SUCCESS)
        throw std::runtime_error("Failed to create graphics pipeline!");

    return pipeline;
}
