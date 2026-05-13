#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace vkfw {

class VkContext;

enum class ShaderStage {
    Vertex,
    Fragment,
    Geometry,
    TessellationControl,
    TessellationEvaluation,
    Compute
};

struct ShaderInfo {
    std::vector<uint32_t> spirv_code;
    ShaderStage stage = ShaderStage::Vertex;
    std::string entry_point = "main";
};

class VkShader {
public:
    VkShader() = default;
    ~VkShader() = default;

    VkShader(VkShader&&) noexcept = default;
    VkShader& operator=(VkShader&&) noexcept = default;

    VkShader(VkShader const&) = delete;
    VkShader& operator=(VkShader const&) = delete;

    bool Init(VkContext& ctx, ShaderInfo const& info);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return shader_module_ != nullptr; }
    
    vk::ShaderModule Handle() const noexcept { return *shader_module_; }
    ShaderStage Stage() const noexcept { return stage_; }
    std::string const& EntryPoint() const noexcept { return entry_point_; }

    vk::PipelineShaderStageCreateInfo GetPipelineStageInfo() const;

private:
    std::unique_ptr<vk::raii::ShaderModule> shader_module_;
    ShaderStage stage_ = ShaderStage::Vertex;
    std::string entry_point_ = "main";
};

} // namespace vkfw
