#include "VkDescriptor.hpp"
#include "VkContext.hpp"

namespace vkfw {

static vk::DescriptorType GetDescriptorType(DescriptorType type) {
    switch (type) {
        case DescriptorType::UniformBuffer:
            return vk::DescriptorType::eUniformBuffer;
        case DescriptorType::StorageBuffer:
            return vk::DescriptorType::eStorageBuffer;
        case DescriptorType::ImageSampler:
            return vk::DescriptorType::eSampler;
        case DescriptorType::CombinedImageSampler:
            return vk::DescriptorType::eCombinedImageSampler;
        case DescriptorType::StorageImage:
            return vk::DescriptorType::eStorageImage;
        default:
            return vk::DescriptorType::eUniformBuffer;
    }
}

static vk::DescriptorPoolSize GetPoolSize(DescriptorBinding const& binding, uint32_t max_sets) {
    vk::DescriptorPoolSize size{};
    size.type = GetDescriptorType(binding.type);
    size.descriptorCount = binding.descriptor_count * max_sets;
    return size;
}

bool VkDescriptorSetLayout::Init(VkContext& ctx, DescriptorSetLayoutInfo const& info) {
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    bindings.reserve(info.bindings.size());

    for (auto const& binding : info.bindings) {
        vk::DescriptorSetLayoutBinding layout_binding{};
        layout_binding.binding = binding.binding;
        layout_binding.descriptorType = GetDescriptorType(binding.type);
        layout_binding.descriptorCount = binding.descriptor_count;
        layout_binding.stageFlags = binding.stage_flags;
        bindings.push_back(layout_binding);
    }

    vk::DescriptorSetLayoutCreateInfo layout_info{};
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();

    try {
        layout_ = std::make_unique<vk::raii::DescriptorSetLayout>(ctx.Device(), layout_info);
        return true;
    } catch (vk::SystemError& e) {
        return false;
    }
}

void VkDescriptorSetLayout::Shutdown(VkContext& ctx) {
    layout_.reset();
}

bool VkDescriptorPool::Init(VkContext& ctx, std::vector<DescriptorBinding> const& bindings, uint32_t max_sets) {
    std::vector<vk::DescriptorPoolSize> pool_sizes;
    pool_sizes.reserve(bindings.size());

    for (auto const& binding : bindings) {
        pool_sizes.push_back(GetPoolSize(binding, max_sets));
    }

    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.maxSets = max_sets;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();

    try {
        pool_ = std::make_unique<vk::raii::DescriptorPool>(ctx.Device(), pool_info);
        return true;
    } catch (vk::SystemError& e) {
        return false;
    }
}

void VkDescriptorPool::Shutdown(VkContext& ctx) {
    pool_.reset();
}

std::vector<vk::raii::DescriptorSet> VkDescriptorPool::AllocateDescriptorSets(VkContext& ctx, uint32_t count, 
    vk::DescriptorSetLayout const* layouts) {
    vk::DescriptorSetAllocateInfo alloc_info{};
    alloc_info.descriptorPool = *pool_;
    alloc_info.descriptorSetCount = count;
    alloc_info.pSetLayouts = layouts;

    return ctx.Device().allocateDescriptorSets(alloc_info);
}

} // namespace vkfw
