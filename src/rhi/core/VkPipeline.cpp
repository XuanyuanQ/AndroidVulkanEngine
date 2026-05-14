#include "VkPipeline.hpp"
#include "VkContext.hpp"
#include "VkDescriptor.hpp"

namespace vkfw {

static vk::VertexInputRate GetVertexInputRate(uint32_t stride) {
    (void)stride;
    return vk::VertexInputRate::eVertex;
}

bool VkPipelineLayout::Init(VkContext& ctx, std::vector<VkDescriptorSetLayout*> const& descriptor_layouts) {
    std::vector<vk::DescriptorSetLayout> layouts;
    layouts.reserve(descriptor_layouts.size());
    
    for (auto const* layout : descriptor_layouts) {
        if (layout && layout->IsInitialized()) {
            layouts.push_back(layout->Handle());
        }
    }

    vk::PipelineLayoutCreateInfo layout_info{};
    layout_info.setLayoutCount = static_cast<uint32_t>(layouts.size());
    layout_info.pSetLayouts = layouts.data();

    try {
        layout_ = std::make_unique<vk::raii::PipelineLayout>(ctx.Device(), layout_info);
        return true;
    } catch (vk::SystemError& e) {
        return false;
    }
}

void VkPipelineLayout::Shutdown(VkContext& ctx) {
    layout_.reset();
}

bool VkPipeline::Init(VkContext& ctx, PipelineInfo const& info) {
    // Convert vertex input state
    std::vector<vk::VertexInputBindingDescription> vertex_bindings;
    std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
    
    for (auto const& input : info.vertex_input.vertex_inputs) {
        vk::VertexInputBindingDescription binding{};
        binding.binding = input.binding;
        binding.stride = input.stride;
        binding.inputRate = GetVertexInputRate(input.stride);
        vertex_bindings.push_back(binding);

        vk::VertexInputAttributeDescription attribute{};
        attribute.location = input.location;
        attribute.binding = input.binding;
        attribute.format = input.format;
        attribute.offset = input.offset;
        vertex_attributes.push_back(attribute);
    }

    vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
    vertex_input_state.vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_bindings.size());
    vertex_input_state.pVertexBindingDescriptions = vertex_bindings.data();
    vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attributes.size());
    vertex_input_state.pVertexAttributeDescriptions = vertex_attributes.data();

    // Convert input assembly state
    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = info.vertex_input.topology;
    input_assembly.primitiveRestartEnable = info.vertex_input.primitive_restart_enable;

    // Convert viewport state
    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = static_cast<uint32_t>(info.viewport.viewports.size());
    viewport_state.pViewports = info.viewport.viewports.data();
    viewport_state.scissorCount = static_cast<uint32_t>(info.viewport.scissors.size());
    viewport_state.pScissors = info.viewport.scissors.data();

    // Convert rasterization state
    vk::PipelineRasterizationStateCreateInfo rasterization{};
    rasterization.polygonMode = info.rasterization.polygon_mode;
    rasterization.cullMode = info.rasterization.cull_mode;
    rasterization.frontFace = info.rasterization.front_face;
    rasterization.lineWidth = info.rasterization.line_width;

    // Convert multisample state
    vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.rasterizationSamples = info.multisample.rasterization_samples;
    multisample.sampleShadingEnable = info.multisample.sample_shading_enable;
    multisample.minSampleShading = info.multisample.min_sample_shading;
    multisample.pSampleMask = &info.multisample.sample_mask;
    multisample.alphaToCoverageEnable = info.multisample.alpha_to_coverage_enable;
    multisample.alphaToOneEnable = info.multisample.alpha_to_one_enable;

    // Convert depth stencil state
    vk::PipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.depthTestEnable = info.depth_stencil.depth_test_enable;
    depth_stencil.depthWriteEnable = info.depth_stencil.depth_write_enable;
    depth_stencil.depthCompareOp = info.depth_stencil.depth_compare_op;
    depth_stencil.depthBoundsTestEnable = info.depth_stencil.depth_bounds_test_enable;
    depth_stencil.minDepthBounds = info.depth_stencil.min_depth_bounds;
    depth_stencil.maxDepthBounds = info.depth_stencil.max_depth_bounds;
    depth_stencil.stencilTestEnable = info.depth_stencil.stencil_test_enable;
    depth_stencil.front = info.depth_stencil.front;
    depth_stencil.back = info.depth_stencil.back;

    // Convert color blend state
    std::vector<vk::PipelineColorBlendAttachmentState> blend_attachments;
    for (auto const& attachment : info.color_blend.attachments) {
        vk::PipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.blendEnable = attachment.blend_enable;
        blend_attachment.srcColorBlendFactor = attachment.src_color_blend;
        blend_attachment.dstColorBlendFactor = attachment.dst_color_blend;
        blend_attachment.colorBlendOp = attachment.color_blend_op;
        blend_attachment.srcAlphaBlendFactor = attachment.src_alpha_blend;
        blend_attachment.dstAlphaBlendFactor = attachment.dst_alpha_blend;
        blend_attachment.alphaBlendOp = attachment.alpha_blend_op;
        blend_attachment.colorWriteMask = attachment.color_write_mask;
        blend_attachments.push_back(blend_attachment);
    }

    vk::PipelineColorBlendStateCreateInfo color_blend{};
    color_blend.logicOpEnable = info.color_blend.logic_op_enable;
    color_blend.logicOp = info.color_blend.logic_op;
    color_blend.attachmentCount = static_cast<uint32_t>(blend_attachments.size());
    color_blend.pAttachments = blend_attachments.data();
    color_blend.blendConstants[0] = info.color_blend.blend_constants[0];
    color_blend.blendConstants[1] = info.color_blend.blend_constants[1];
    color_blend.blendConstants[2] = info.color_blend.blend_constants[2];
    color_blend.blendConstants[3] = info.color_blend.blend_constants[3];

    // Get shader stage info
    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
    for (auto const& shader : info.shaders) {
        if (shader.IsInitialized()) {
            shader_stages.push_back(shader.GetPipelineStageInfo());
        }
    }

    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    pipeline_info.pStages = shader_stages.data();
    pipeline_info.pVertexInputState = &vertex_input_state;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.layout = info.layout;
    pipeline_info.renderPass = info.render_pass;
    pipeline_info.subpass = info.subpass;
    pipeline_info.basePipelineHandle = info.base_pipeline_handle;
    pipeline_info.basePipelineIndex = info.base_pipeline_index;

    try {
        pipeline_ = std::make_unique<vk::raii::Pipeline>(ctx.Device(), nullptr, pipeline_info);
        bind_point_ = vk::PipelineBindPoint::eGraphics;
        return true;
    } catch (vk::SystemError& e) {
        return false;
    }
}

void VkPipeline::Shutdown(VkContext& ctx) {
    pipeline_.reset();
    bind_point_ = vk::PipelineBindPoint::eGraphics;
}

} // namespace vkfw
