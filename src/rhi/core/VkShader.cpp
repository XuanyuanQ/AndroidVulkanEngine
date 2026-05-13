#include "VkShader.hpp"
#include "VkContext.hpp"

namespace vkfw {

static vk::ShaderStageFlagBits GetShaderStageFlag(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:
            return vk::ShaderStageFlagBits::eVertex;
        case ShaderStage::Fragment:
            return vk::ShaderStageFlagBits::eFragment;
        case ShaderStage::Geometry:
            return vk::ShaderStageFlagBits::eGeometry;
        case ShaderStage::TessellationControl:
            return vk::ShaderStageFlagBits::eTessellationControl;
        case ShaderStage::TessellationEvaluation:
            return vk::ShaderStageFlagBits::eTessellationEvaluation;
        case ShaderStage::Compute:
            return vk::ShaderStageFlagBits::eCompute;
        default:
            return vk::ShaderStageFlagBits::eVertex;
    }
}

bool VkShader::Init(VkContext& ctx, ShaderInfo const& info) {
    stage_ = info.stage;
    entry_point_ = info.entry_point;

    vk::ShaderModuleCreateInfo create_info{};
    create_info.codeSize = info.spirv_code.size() * sizeof(uint32_t);
    create_info.pCode = info.spirv_code.data();

    try {
        shader_module_ = std::make_unique<vk::raii::ShaderModule>(ctx.Device(), create_info);
        return true;
    } catch (vk::SystemError& e) {
        return false;
    }
}

void VkShader::Shutdown(VkContext& /*ctx*/) {
    shader_module_.reset();
    stage_ = ShaderStage::Vertex;
    entry_point_ = "main";
}

vk::PipelineShaderStageCreateInfo VkShader::GetPipelineStageInfo() const {
    vk::PipelineShaderStageCreateInfo stage_info{};
    stage_info.stage = GetShaderStageFlag(stage_);
    stage_info.module = *shader_module_;
    stage_info.pName = entry_point_.c_str();
    return stage_info;
}

} // namespace vkfw
