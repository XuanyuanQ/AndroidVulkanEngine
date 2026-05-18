#include "ave/render/PipelineSystem.h"
#include "ave/render/MaterialSystem.h"
#include "ave/resource/ResourceSystem.h"
#include "VkDescriptor.hpp"
#include "VkPipeline.hpp"

namespace ave::render {
namespace {

DescriptorSetLayoutKey MakeFrameSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::UniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eAllGraphics),
        },
        // Reserved for shadow map / global textures.
        DescriptorBinding{
            .binding = 1,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
    };
    return key;
}

DescriptorSetLayoutKey MakeMaterialSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::UniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 1,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        // Reserved slots for common PBR textures.
        DescriptorBinding{
            .binding = 2,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 3,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
    };
    return key;
}

DescriptorSetLayoutKey MakeObjectSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::StorageBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment),
        },
    };
    return key;
}

} // namespace

// Descriptor Set Layout Cache
DescriptorSetLayoutCache::DescriptorSetLayoutCache() = default;

uint32_t DescriptorSetLayoutCache::GetOrCreateLayout(DescriptorSetLayoutKey const& key)
{
    if (!ctx_) {
        return 0;
    }
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }
    
    uint32_t id = next_id_++;
    
    // Convert bindings to vkfw format
    vkfw::DescriptorSetLayoutInfo layout_info;
    for (auto const& binding : key.bindings) {
        vkfw::DescriptorBinding vkfw_binding;
        vkfw_binding.binding = binding.binding;
        vkfw_binding.type = static_cast<vkfw::DescriptorType>(binding.descriptor_type);
        vkfw_binding.descriptor_count = binding.descriptor_count;
        vkfw_binding.stage_flags = static_cast<vk::ShaderStageFlags>(binding.stage_flags);
        layout_info.bindings.push_back(vkfw_binding);
    }
    
    // Create VkDescriptorSetLayout using vkfw
    auto layout = std::make_unique<vkfw::VkDescriptorSetLayout>();
    if (layout->Init(*ctx_, layout_info)) {
        layouts_[id] = std::move(layout);
    }
    
    cache_[key] = id;
    return id;
}

vkfw::VkDescriptorSetLayout const* DescriptorSetLayoutCache::GetLayout(uint32_t id) const
{
    auto it = layouts_.find(id);
    if (it != layouts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

vkfw::VkDescriptorSetLayout* DescriptorSetLayoutCache::GetLayoutMutable(uint32_t id)
{
    auto it = layouts_.find(id);
    if (it != layouts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void DescriptorSetLayoutCache::Clear()
{
    if (ctx_) {
        for (auto& [id, layout] : layouts_) {
            layout->Shutdown(*ctx_);
        }
    }
    layouts_.clear();
    cache_.clear();
    next_id_ = 1;
}

// Pipeline Layout Cache
PipelineLayoutCache::PipelineLayoutCache() = default;

uint32_t PipelineLayoutCache::GetOrCreateLayout(PipelineLayoutKey const& key)
{
    if (!ctx_) {
        return 0;
    }
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }
    
    uint32_t id = next_id_++;
    
    // Collect descriptor set layouts from cache
    std::vector<vkfw::VkDescriptorSetLayout*> set_layouts;
    if (desc_set_layout_cache_) {
        set_layouts.reserve(key.set_layout_ids.size());
        for (uint32_t set_layout_id : key.set_layout_ids) {
            if (set_layout_id == 0) {
                continue;
            }
            if (auto* layout = desc_set_layout_cache_->GetLayoutMutable(set_layout_id)) {
                set_layouts.push_back(layout);
            }
        }
    }
    
    // Create VkPipelineLayout using vkfw
    auto layout = std::make_unique<vkfw::VkPipelineLayout>();
    if (layout->Init(*ctx_, set_layouts)) {
        layouts_[id] = std::move(layout);
    }
    
    cache_[key] = id;
    return id;
}

vkfw::VkPipelineLayout const* PipelineLayoutCache::GetLayout(uint32_t id) const
{
    auto it = layouts_.find(id);
    if (it != layouts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void PipelineLayoutCache::Clear()
{
    if (ctx_) {
        for (auto& [id, layout] : layouts_) {
            layout->Shutdown(*ctx_);
        }
    }
    layouts_.clear();
    cache_.clear();
    next_id_ = 1;
}

// Pipeline Cache
PipelineCache::PipelineCache() = default;

uint32_t PipelineCache::GetOrCreatePipeline(PipelineKey const& key)
{
    if (!ctx_) {
        return 0;
    }
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }
    
    uint32_t id = next_id_++;
    
    // Create VkPipeline using vkfw
    auto pipeline = std::make_unique<vkfw::VkPipeline>();
    vkfw::PipelineInfo pipeline_info;
    
    // Get shader stages from ResourceSystem
    if (resource_system_) {
        auto const* shader = resource_system_->GetShaderManager().GetShader(key.shader_id);
        if (shader && shader->vertex_shader && shader->vertex_shader->IsInitialized()) {
            pipeline_info.shader_stages.push_back(shader->vertex_shader->GetPipelineStageInfo());
        }
        if (shader && shader->fragment_shader && shader->fragment_shader->IsInitialized()) {
            pipeline_info.shader_stages.push_back(shader->fragment_shader->GetPipelineStageInfo());
        }
    }
    
    // Set vertex input state (simplified for now)
    vkfw::PipelineVertexInputState vertex_input;
    vertex_input.topology = vk::PrimitiveTopology::eTriangleList;
    if (key.vertex_layout_id == 1) {
        // RasterColorVertex: position(float3) + color(float4)
        vertex_input.vertex_inputs = {
            vkfw::PipelineVertexInput{
                .binding = 0,
                .location = 0,
                .stride = 7u * sizeof(float),
                .format = vk::Format::eR32G32B32Sfloat,
                .offset = 0,
            },
            vkfw::PipelineVertexInput{
                .binding = 0,
                .location = 1,
                .stride = 7u * sizeof(float),
                .format = vk::Format::eR32G32B32A32Sfloat,
                .offset = 3u * sizeof(float),
            },
        };
    }
    pipeline_info.vertex_input = vertex_input;
    
    // Set rasterization state
    vkfw::PipelineRasterizationState rasterization;
    rasterization.polygon_mode = vk::PolygonMode::eFill;
    // Match the demo expectations (accept both windings) until we introduce proper render state keys.
    rasterization.cull_mode = vk::CullModeFlagBits::eNone;
    pipeline_info.rasterization = rasterization;

    // Demo pipeline defaults: no depth attachment in the swapchain pass, so disable depth.
    pipeline_info.depth_stencil.depth_test_enable = false;
    pipeline_info.depth_stencil.depth_write_enable = false;

    // Swapchain render pass has one color attachment; provide one blend attachment state.
    pipeline_info.color_blend.attachments = {vkfw::PipelineColorBlendAttachment{}};
    
    // Set pipeline layout (fixed engine convention; caller chooses a profile via PipelineKey::layout_profile)
    if (pipeline_layout_cache_) {
        PipelineLayoutKey layout_key;

        uint32_t frame_set_layout_id = 0;
        uint32_t material_set_layout_id = 0;
        uint32_t object_set_layout_id = 0;

        if (auto* desc_cache = pipeline_layout_cache_->GetDescriptorSetLayoutCache()) {
            if (key.layout_profile == 1 || key.layout_profile == 2 || key.layout_profile == 3) {
                frame_set_layout_id = desc_cache->GetOrCreateLayout(MakeFrameSetLayoutKey());
            }
            if (key.layout_profile == 1 || key.layout_profile == 2) {
                material_set_layout_id = desc_cache->GetOrCreateLayout(MakeMaterialSetLayoutKey());
            }
            if (key.layout_profile == 1) {
                object_set_layout_id = desc_cache->GetOrCreateLayout(MakeObjectSetLayoutKey());
            }
        }

        if (frame_set_layout_id != 0) {
            layout_key.set_layout_ids.push_back(frame_set_layout_id);
        }
        if (material_set_layout_id != 0) {
            layout_key.set_layout_ids.push_back(material_set_layout_id);
        }
        if (object_set_layout_id != 0) {
            layout_key.set_layout_ids.push_back(object_set_layout_id);
        }

        uint32_t layout_id = pipeline_layout_cache_->GetOrCreateLayout(layout_key);
        auto const* layout = pipeline_layout_cache_->GetLayout(layout_id);
        if (layout) {
            pipeline_info.layout = layout->Handle();
        }
    }
    
    // Set viewport/scissor
    if (key.viewport_width > 0 && key.viewport_height > 0) {
        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(key.viewport_width);
        viewport.height = static_cast<float>(key.viewport_height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{0, 0};
        scissor.extent = vk::Extent2D{key.viewport_width, key.viewport_height};

        pipeline_info.viewport.viewports = {viewport};
        pipeline_info.viewport.scissors = {scissor};
    }

    // Dynamic rendering: pipeline is keyed by attachment formats (no VkRenderPass).
    pipeline_info.use_dynamic_rendering = true;
    pipeline_info.color_formats = {static_cast<vk::Format>(key.rt_format)};
    pipeline_info.depth_format = static_cast<vk::Format>(key.depth_format);
    pipeline_info.stencil_format = static_cast<vk::Format>(key.stencil_format);
    
    // Initialize pipeline
    if (!pipeline->Init(*ctx_, pipeline_info)) {
        return 0;
    }

    pipelines_[id] = std::move(pipeline);
    cache_[key] = id;
    return id;
}

vkfw::VkPipeline const* PipelineCache::GetPipeline(uint32_t id) const
{
    auto it = pipelines_.find(id);
    if (it != pipelines_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void PipelineCache::Clear()
{
    if (ctx_) {
        for (auto& [id, pipeline] : pipelines_) {
            pipeline->Shutdown(*ctx_);
        }
    }
    pipelines_.clear();
    cache_.clear();
    next_id_ = 1;
}

// Descriptor Allocator
DescriptorAllocator::DescriptorAllocator() = default;

uint32_t DescriptorAllocator::AllocateDescriptorSet(uint32_t layout_id)
{
    if (!ctx_ || layout_id == 0) {
        return 0;
    }
    
    if (!free_sets_.empty()) {
        uint32_t id = free_sets_.back();
        free_sets_.pop_back();
        return id;
    }
    
    uint32_t id = next_set_id_++;
    
    // Create descriptor pool if not exists
    if (!pool_) {
        vkfw::DescriptorBinding pool_binding;
        pool_binding.binding = 0;
        pool_binding.type = vkfw::DescriptorType::UniformBuffer;
        pool_binding.descriptor_count = 1000;
        pool_binding.stage_flags = vk::ShaderStageFlagBits::eAll;
        
        pool_ = std::make_unique<vkfw::VkDescriptorPool>();
        std::vector<vkfw::DescriptorBinding> pool_bindings = {pool_binding};
        pool_->Init(*ctx_, pool_bindings, 100);
    }
    
    if (desc_set_layout_cache_) {
        if (auto const* layout = desc_set_layout_cache_->GetLayout(layout_id)) {
            vk::DescriptorSetLayout const layout_handle = layout->Handle();
            try {
                auto sets = pool_->AllocateDescriptorSets(*ctx_, 1, &layout_handle);
                if (sets.empty()) {
                    return 0;
                }
                // Keep raw handle; RAII wrapper is short-lived here.
                // sets_[id] = *sets[0];
            } catch (...) {
                return 0;
            }
        }
    }
    
    return id;
}

void DescriptorAllocator::FreeDescriptorSet(uint32_t set_id)
{
    // sets_.erase(set_id);
    free_sets_.push_back(set_id);
}

void DescriptorAllocator::UpdateDescriptorSet(uint32_t set_id, std::vector<DescriptorBinding> const& bindings)
{
    if (!ctx_ || !pool_) {
        return;
    }
    
    // Update descriptor set using vkfw
    // Note: This requires the actual VkDescriptorSet handle and buffer/image resources
    // For now, this is a placeholder - the actual implementation would:
    // 1. Get the VkDescriptorSet handle from the set_id
    // 2. Build vk::WriteDescriptorSet structures from the bindings
    // 3. Call vkUpdateDescriptorSets with the write structures
    // This requires storing the actual descriptor set handles and resource references
}

void DescriptorAllocator::Clear()
{
    free_sets_.clear();
    // sets_.clear();
    next_set_id_ = 1;
}

// vk::DescriptorSet DescriptorAllocator::GetHandle(uint32_t set_id) const
// {
//     auto it = sets_.find(set_id);
//     if (it == sets_.end()) {
//         return {};
//     }
//     return it->second;
// }

// Pipeline System
PipelineSystem::PipelineSystem() = default;

void PipelineSystem::SetContext(vkfw::VkContext* ctx)
{
    ctx_ = ctx;
    desc_set_layout_cache_.SetContext(ctx);
    pipeline_layout_cache_.SetContext(ctx);
    pipeline_layout_cache_.SetDescriptorSetLayoutCache(&desc_set_layout_cache_);
    pipeline_cache_.SetContext(ctx);
    pipeline_cache_.SetPipelineLayoutCache(&pipeline_layout_cache_);
    descriptor_allocator_.SetContext(ctx);
    descriptor_allocator_.SetDescriptorSetLayoutCache(&desc_set_layout_cache_);
}

void PipelineSystem::SetResourceSystem(ave::resource::ResourceSystem* resource_system)
{
    pipeline_cache_.SetResourceSystem(resource_system);
}

void PipelineSystem::Clear()
{
    desc_set_layout_cache_.Clear();
    pipeline_layout_cache_.Clear();
    pipeline_cache_.Clear();
    descriptor_allocator_.Clear();
}

} // namespace ave::render
