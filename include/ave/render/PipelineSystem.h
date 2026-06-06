#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <glm/glm.hpp>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace ave::resource {
class ResourceSystem;
}

namespace vkfw {
class VkContext;
class VkPipeline;
class VkPipelineLayout;
class VkDescriptorSetLayout;
class VkDescriptorPool;
}

namespace ave::render {

/**
     * @brief 定义着色器管线布局的配置 Profile
     * * 用于规范不同渲染批次或 Pass 所需的 Descriptor Set 数量与层级结构。
     */
    enum class PipelineLayoutProfile : uint32_t {
        /// @brief 空布局：不绑定任何 Descriptor Set（常用于极简 Demo 或纯顶点动画）
        Empty = 0,

        /// @brief 完整布局：包含 Set 0 (Frame) + Set 1 (Material) + Set 2 (Object)
        Full_Set0_Set1_Set2 = 1,

        /// @brief 材质布局：包含 Set 0 (Frame) + Set 1 (Material)
        Material_Set0_Set1 = 2,

        /// @brief 仅全局布局：只包含 Set 0 (Frame / Global)（常用于后处理 Post-Processing）
        Global_Set0_Only = 3,

        /// @brief 计算空间剔除布局：仅包含一个用于 Compute Shader 的 Set 0（内含输入的物体列表和输出的剔除结果 SSBO）
        ComputeCulling_Set0_Only = 4,

        /// @brief UI 贴图布局：仅包含一个 Set 0（单个 combined image sampler）
        Texture_Set0_Only = 5,
    };

// Pipeline key for caching
struct PipelineKey {
    uint32_t shader_id = 0;
    uint32_t vertex_layout_id = 0;//暂时不区分顶点布局，后续可根据需要添加
    uint32_t render_state_id = 0;
    // Descriptor layout profile.
    // 0: empty (no descriptor sets)
    // 1: Set0 frame + Set1 material + Set2 object
    // 2: Set0 frame + Set1 material
    // 3: Set0 frame only
    ave::render::PipelineLayoutProfile layout_profile = ave::render::PipelineLayoutProfile::Empty;
    uint32_t rt_format = 0;
    uint32_t depth_format = 0;
    uint32_t stencil_format = 0;
    uint32_t sample_count = 1;
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
    
    bool operator==(PipelineKey const& other) const {
        return  shader_id == other.shader_id &&
               vertex_layout_id == other.vertex_layout_id &&
               render_state_id == other.render_state_id &&
               layout_profile == other.layout_profile &&
               rt_format == other.rt_format &&
               depth_format == other.depth_format &&
               stencil_format == other.stencil_format &&
               sample_count == other.sample_count &&
               viewport_width == other.viewport_width &&
               viewport_height == other.viewport_height;
    }
};

struct UiVertex {
    glm::vec2 position;
    glm::vec2 uv;
    glm::vec4 color;
    uint32_t texture_index;
};

// Hash for PipelineKey
struct PipelineKeyHash {
    std::size_t operator()(PipelineKey const& key) const {
        auto combine = [](std::size_t& seed, std::size_t v) {
            // 64-bit hash combine (boost-like).
            seed ^= v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        };

        std::size_t seed = 0;
        combine(seed, key.shader_id);
        combine(seed, key.vertex_layout_id);
        combine(seed, key.render_state_id);
        combine(seed, static_cast<std::size_t>(key.layout_profile));
        combine(seed, key.rt_format);
        combine(seed, key.depth_format);
        combine(seed, key.stencil_format);
        combine(seed, key.sample_count);
        combine(seed, key.viewport_width);
        combine(seed, key.viewport_height);
        return seed;
    }
};

// Descriptor set layout binding
struct DescriptorBinding {
    uint32_t binding = 0;
    uint32_t descriptor_type = 0; // vk::DescriptorType
    uint32_t descriptor_count = 1;
    uint32_t stage_flags = 0; // vk::ShaderStageFlags
};

// Descriptor set layout key
struct DescriptorSetLayoutKey {
    std::vector<DescriptorBinding> bindings;
    
    bool operator==(DescriptorSetLayoutKey const& other) const {
        if (bindings.size() != other.bindings.size()) {
            return false;
        }
        for (size_t i = 0; i < bindings.size(); ++i) {
            if (bindings[i].binding != other.bindings[i].binding ||
                bindings[i].descriptor_type != other.bindings[i].descriptor_type ||
                bindings[i].descriptor_count != other.bindings[i].descriptor_count ||
                bindings[i].stage_flags != other.bindings[i].stage_flags) {
                return false;
            }
        }
        return true;
    }
};

// Hash for DescriptorSetLayoutKey
struct DescriptorSetLayoutKeyHash {
    std::size_t operator()(DescriptorSetLayoutKey const& key) const {
        std::size_t hash = 0;
        for (auto const& binding : key.bindings) {
            hash ^= static_cast<std::size_t>(binding.binding);
            hash ^= static_cast<std::size_t>(binding.descriptor_type) << 8;
            hash ^= static_cast<std::size_t>(binding.descriptor_count) << 16;
            hash ^= static_cast<std::size_t>(binding.stage_flags) << 24;
        }
        return hash;
    }
};

// Pipeline layout key
struct PipelineLayoutKey {
    std::vector<uint32_t> set_layout_ids;
    
    bool operator==(PipelineLayoutKey const& other) const {
        return set_layout_ids == other.set_layout_ids;
    }
};

DescriptorSetLayoutKey MakeFrameSetLayoutKey();
DescriptorSetLayoutKey MakeMaterialSetLayoutKey();
DescriptorSetLayoutKey MakeObjectSetLayoutKey();
DescriptorSetLayoutKey MakeTextureSetLayoutKey();

// Hash for PipelineLayoutKey
struct PipelineLayoutKeyHash {
    std::size_t operator()(PipelineLayoutKey const& key) const {
        std::size_t hash = 0;
        for (uint32_t id : key.set_layout_ids) {
            hash ^= static_cast<std::size_t>(id);
        }
        return hash;
    }
};

// Descriptor set layout cache
class DescriptorSetLayoutCache {
public:
    DescriptorSetLayoutCache();
    ~DescriptorSetLayoutCache();
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    
    uint32_t GetOrCreateLayout(DescriptorSetLayoutKey const& key);
    vkfw::VkDescriptorSetLayout const* GetLayout(uint32_t id) const;
    vkfw::VkDescriptorSetLayout* GetLayoutMutable(uint32_t id);
    size_t LayoutCount() const noexcept { return layouts_.size(); }
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    std::unordered_map<DescriptorSetLayoutKey, uint32_t, DescriptorSetLayoutKeyHash> cache_;
    std::unordered_map<uint32_t, std::unique_ptr<vkfw::VkDescriptorSetLayout>> layouts_;
    uint32_t next_id_ = 1;
};

// Pipeline layout cache
class PipelineLayoutCache {
public:
    PipelineLayoutCache();
    ~PipelineLayoutCache();
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    void SetDescriptorSetLayoutCache(DescriptorSetLayoutCache* cache) { desc_set_layout_cache_ = cache; }
    DescriptorSetLayoutCache* GetDescriptorSetLayoutCache() const { return desc_set_layout_cache_; }
    
    uint32_t GetOrCreateLayout(PipelineLayoutKey const& key, std::vector<vk::PushConstantRange> const& push_constants = {});
    vkfw::VkPipelineLayout const* GetLayout(uint32_t id) const;
    size_t LayoutCount() const noexcept { return layouts_.size(); }
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    DescriptorSetLayoutCache* desc_set_layout_cache_ = nullptr;
    std::unordered_map<PipelineLayoutKey, uint32_t, PipelineLayoutKeyHash> cache_;
    std::unordered_map<uint32_t, std::unique_ptr<vkfw::VkPipelineLayout>> layouts_;
    uint32_t next_id_ = 1;
};

// Pipeline cache
class PipelineCache {
public:
    PipelineCache();
    ~PipelineCache();
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    void SetResourceSystem(ave::resource::ResourceSystem* resource_system) { resource_system_ = resource_system; }
    void SetPipelineLayoutCache(PipelineLayoutCache* pipeline_layout_cache) { pipeline_layout_cache_ = pipeline_layout_cache; }
    
    uint32_t GetOrCreatePipeline(PipelineKey const& key, vk::RenderPass compatibility_render_pass = {});
    vkfw::VkPipeline const* GetPipeline(uint32_t id) const;
    size_t PipelineCount() const noexcept { return pipelines_.size(); }
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    ave::resource::ResourceSystem* resource_system_ = nullptr;
    PipelineLayoutCache* pipeline_layout_cache_ = nullptr;
    std::unordered_map<PipelineKey, uint32_t, PipelineKeyHash> cache_;
    std::unordered_set<PipelineKey, PipelineKeyHash> failed_cache_;
    std::unordered_map<uint32_t, std::unique_ptr<vkfw::VkPipeline>> pipelines_;
    uint32_t next_id_ = 1;
};

// Descriptor allocator
class DescriptorAllocator {
public:
    DescriptorAllocator();
    ~DescriptorAllocator();
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    void SetDescriptorSetLayoutCache(DescriptorSetLayoutCache* cache) { desc_set_layout_cache_ = cache; }
    
    uint32_t AllocateDescriptorSet(uint32_t layout_id);
    void FreeDescriptorSet(uint32_t set_id);

    vk::DescriptorSet GetHandle(uint32_t set_id) const;
    size_t AllocatedSetCount() const noexcept { return sets_.size(); }
    size_t FreeSetCount() const noexcept { return free_sets_.size(); }
    bool UpdateUniformBuffer(uint32_t set_id, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);
    bool UpdateStorageBuffer(uint32_t set_id, uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);

    bool UpdateImageSampler(uint32_t set_id,
                            uint32_t binding,
                            vk::Sampler sampler,
                            vk::ImageView image_view,
                            vk::ImageLayout image_layout);
    bool UpdateImageSamplerArray(uint32_t set_id,
                                 uint32_t binding,
                                 uint32_t array_index,
                                 vk::Sampler sampler,
                                 vk::ImageView image_view,
                                 vk::ImageLayout image_layout);
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    DescriptorSetLayoutCache* desc_set_layout_cache_ = nullptr;
    std::unique_ptr<vkfw::VkDescriptorPool> pool_;
    std::vector<uint32_t> free_sets_;
    std::unordered_map<uint32_t, vk::raii::DescriptorSet> sets_;
    //暂未实现 需要考虑下直接用VkDescriptorSet
    // std::unordered_map<uint32_t, vk::DescriptorSet> sets_;
    uint32_t next_set_id_ = 1;
};

// Unified pipeline system
class PipelineSystem {
public:
    PipelineSystem();
    ~PipelineSystem();
    
    void SetContext(vkfw::VkContext* ctx);
    void SetResourceSystem(ave::resource::ResourceSystem* resource_system);
    
    // Access subsystems
    DescriptorSetLayoutCache& GetDescriptorSetLayoutCache() { return desc_set_layout_cache_; }
    PipelineLayoutCache& GetPipelineLayoutCache() { return pipeline_layout_cache_; }
    PipelineCache& GetPipelineCache() { return pipeline_cache_; }
    DescriptorAllocator& GetDescriptorAllocator() { return descriptor_allocator_; }
    size_t DescriptorSetLayoutCount() const noexcept { return desc_set_layout_cache_.LayoutCount(); }
    size_t PipelineLayoutCount() const noexcept { return pipeline_layout_cache_.LayoutCount(); }
    size_t PipelineCount() const noexcept { return pipeline_cache_.PipelineCount(); }
    size_t AllocatedDescriptorSetCount() const noexcept { return descriptor_allocator_.AllocatedSetCount(); }
    size_t FreeDescriptorSetCount() const noexcept { return descriptor_allocator_.FreeSetCount(); }
    
    DescriptorSetLayoutCache const& GetDescriptorSetLayoutCache() const { return desc_set_layout_cache_; }
    PipelineLayoutCache const& GetPipelineLayoutCache() const { return pipeline_layout_cache_; }
    PipelineCache const& GetPipelineCache() const { return pipeline_cache_; }
    DescriptorAllocator const& GetDescriptorAllocator() const { return descriptor_allocator_; }
    
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    DescriptorSetLayoutCache desc_set_layout_cache_;
    PipelineLayoutCache pipeline_layout_cache_;
    PipelineCache pipeline_cache_;
    DescriptorAllocator descriptor_allocator_;
};

} // namespace ave::render
