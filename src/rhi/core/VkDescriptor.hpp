#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace vkfw {

class VkContext;

enum class DescriptorType {
    UniformBuffer,
    StorageBuffer,
    ImageSampler,
    CombinedImageSampler,
    StorageImage
};

struct DescriptorBinding {
    uint32_t binding = 0;
    DescriptorType type = DescriptorType::UniformBuffer;
    uint32_t descriptor_count = 1;
    vk::ShaderStageFlags stage_flags = vk::ShaderStageFlagBits::eAll;
};

struct DescriptorSetLayoutInfo {
    std::vector<DescriptorBinding> bindings;
};

class VkDescriptorSetLayout {
public:
    VkDescriptorSetLayout() = default;
    ~VkDescriptorSetLayout() = default;

    VkDescriptorSetLayout(VkDescriptorSetLayout&&) noexcept = default;
    VkDescriptorSetLayout& operator=(VkDescriptorSetLayout&&) noexcept = default;

    VkDescriptorSetLayout(VkDescriptorSetLayout const&) = delete;
    VkDescriptorSetLayout& operator=(VkDescriptorSetLayout const&) = delete;

    bool Init(VkContext& ctx, DescriptorSetLayoutInfo const& info);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return layout_ != nullptr; }
    
    vk::DescriptorSetLayout Handle() const noexcept { return *layout_; }

private:
    std::unique_ptr<vk::raii::DescriptorSetLayout> layout_;
};

class VkDescriptorPool {
public:
    VkDescriptorPool() = default;
    ~VkDescriptorPool() = default;

    VkDescriptorPool(VkDescriptorPool&&) noexcept = default;
    VkDescriptorPool& operator=(VkDescriptorPool&&) noexcept = default;

    VkDescriptorPool(VkDescriptorPool const&) = delete;
    VkDescriptorPool& operator=(VkDescriptorPool const&) = delete;

    bool Init(VkContext& ctx, std::vector<DescriptorBinding> const& bindings, uint32_t max_sets);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return pool_ != nullptr; }
    
    vk::DescriptorPool Handle() const noexcept { return *pool_; }

    std::vector<vk::raii::DescriptorSet> AllocateDescriptorSets(VkContext& ctx, uint32_t count, 
        vk::DescriptorSetLayout const* layouts);

private:
    std::unique_ptr<vk::raii::DescriptorPool> pool_;
};

class VkDescriptorSet {
public:
    VkDescriptorSet() = default;
    ~VkDescriptorSet() = default;

    VkDescriptorSet(VkDescriptorSet&&) noexcept = default;
    VkDescriptorSet& operator=(VkDescriptorSet&&) noexcept = default;

    VkDescriptorSet(VkDescriptorSet const&) = delete;
    VkDescriptorSet& operator=(VkDescriptorSet const&) = delete;

    vk::DescriptorSet Handle() const noexcept { return descriptor_set_; }
    void SetHandle(vk::DescriptorSet descriptor_set) noexcept { descriptor_set_ = descriptor_set; }

private:
    vk::DescriptorSet descriptor_set_ = {};
};

} // namespace vkfw
