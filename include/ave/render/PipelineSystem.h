#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

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

// Pipeline key for caching
struct PipelineKey {
    uint32_t pass_id = 0;
    uint32_t shader_id = 0;
    uint32_t vertex_layout_id = 0;
    uint32_t render_state_id = 0;
    // Descriptor layout profile.
    // 0: empty (no descriptor sets)
    // 1: Set0 frame + Set1 material + Set2 object
    // 2: Set0 frame + Set1 material
    // 3: Set0 frame only
    uint32_t layout_profile = 0;
    uint32_t rt_format = 0;
    uint64_t render_pass = 0;
    uint32_t viewport_width = 0;
    uint32_t viewport_height = 0;
    
    bool operator==(PipelineKey const& other) const {
        return pass_id == other.pass_id &&
               shader_id == other.shader_id &&
               vertex_layout_id == other.vertex_layout_id &&
               render_state_id == other.render_state_id &&
               layout_profile == other.layout_profile &&
               rt_format == other.rt_format &&
               render_pass == other.render_pass &&
               viewport_width == other.viewport_width &&
               viewport_height == other.viewport_height;
    }
};

// Hash for PipelineKey
struct PipelineKeyHash {
    std::size_t operator()(PipelineKey const& key) const {
        std::size_t hash = 0;
        hash ^= static_cast<std::size_t>(key.pass_id);
        hash ^= static_cast<std::size_t>(key.shader_id) << 8;
        hash ^= static_cast<std::size_t>(key.vertex_layout_id) << 16;
        hash ^= static_cast<std::size_t>(key.render_state_id) << 24;
        hash ^= static_cast<std::size_t>(key.layout_profile) << 28;
        hash ^= static_cast<std::size_t>(key.rt_format) << 32;
        hash ^= static_cast<std::size_t>(key.render_pass);
        hash ^= static_cast<std::size_t>(key.viewport_width) << 48;
        hash ^= static_cast<std::size_t>(key.viewport_height) << 56;
        return hash;
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
    ~DescriptorSetLayoutCache() = default;
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    
    uint32_t GetOrCreateLayout(DescriptorSetLayoutKey const& key);
    vkfw::VkDescriptorSetLayout const* GetLayout(uint32_t id) const;
    vkfw::VkDescriptorSetLayout* GetLayoutMutable(uint32_t id);
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
    ~PipelineLayoutCache() = default;
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    void SetDescriptorSetLayoutCache(DescriptorSetLayoutCache* cache) { desc_set_layout_cache_ = cache; }
    DescriptorSetLayoutCache* GetDescriptorSetLayoutCache() const { return desc_set_layout_cache_; }
    
    uint32_t GetOrCreateLayout(PipelineLayoutKey const& key);
    vkfw::VkPipelineLayout const* GetLayout(uint32_t id) const;
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
    ~PipelineCache() = default;
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    void SetResourceSystem(ave::resource::ResourceSystem* resource_system) { resource_system_ = resource_system; }
    void SetPipelineLayoutCache(PipelineLayoutCache* pipeline_layout_cache) { pipeline_layout_cache_ = pipeline_layout_cache; }
    
    uint32_t GetOrCreatePipeline(PipelineKey const& key);
    vkfw::VkPipeline const* GetPipeline(uint32_t id) const;
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    ave::resource::ResourceSystem* resource_system_ = nullptr;
    PipelineLayoutCache* pipeline_layout_cache_ = nullptr;
    std::unordered_map<PipelineKey, uint32_t, PipelineKeyHash> cache_;
    std::unordered_map<uint32_t, std::unique_ptr<vkfw::VkPipeline>> pipelines_;
    uint32_t next_id_ = 1;
};

// Descriptor allocator
class DescriptorAllocator {
public:
    DescriptorAllocator();
    ~DescriptorAllocator() = default;
    
    void SetContext(vkfw::VkContext* ctx) { ctx_ = ctx; }
    void SetDescriptorSetLayoutCache(DescriptorSetLayoutCache* cache) { desc_set_layout_cache_ = cache; }
    
    uint32_t AllocateDescriptorSet(uint32_t layout_id);
    void FreeDescriptorSet(uint32_t set_id);
    void UpdateDescriptorSet(uint32_t set_id, std::vector<DescriptorBinding> const& bindings);
    // vk::DescriptorSet GetHandle(uint32_t set_id) const;
    void Clear();
    
private:
    vkfw::VkContext* ctx_ = nullptr;
    DescriptorSetLayoutCache* desc_set_layout_cache_ = nullptr;
    std::unique_ptr<vkfw::VkDescriptorPool> pool_;
    std::vector<uint32_t> free_sets_;
    //暂未实现 需要考虑下直接用VkDescriptorSet
    // std::unordered_map<uint32_t, vk::DescriptorSet> sets_;
    uint32_t next_set_id_ = 1;
};

// Unified pipeline system
class PipelineSystem {
public:
    PipelineSystem();
    ~PipelineSystem() = default;
    
    void SetContext(vkfw::VkContext* ctx);
    void SetResourceSystem(ave::resource::ResourceSystem* resource_system);
    
    // Access subsystems
    DescriptorSetLayoutCache& GetDescriptorSetLayoutCache() { return desc_set_layout_cache_; }
    PipelineLayoutCache& GetPipelineLayoutCache() { return pipeline_layout_cache_; }
    PipelineCache& GetPipelineCache() { return pipeline_cache_; }
    DescriptorAllocator& GetDescriptorAllocator() { return descriptor_allocator_; }
    
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
