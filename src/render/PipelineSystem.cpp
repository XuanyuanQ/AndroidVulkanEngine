#include "ave/render/PipelineSystem.h"
#include "ave/render/MaterialSystem.h"
#include "ave/project/SharedDataContract.h"
#include "ave/resource/ResourceSystem.h"
#include "VkDescriptor.hpp"
#include "VkPipeline.hpp"
#include "VkContext.hpp"
#include <cstddef>

#include <cstddef>

namespace ave::render {

// Frame descriptor set layout: only Uniform Buffer needed for view_projection
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

DescriptorSetLayoutKey MakeTextureSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
    };
    return key;
}

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

uint32_t PipelineLayoutCache::GetOrCreateLayout(PipelineLayoutKey const& key, std::vector<vk::PushConstantRange> const& push_constants)
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
    if (layout->Init(*ctx_, set_layouts, push_constants)) {
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

uint32_t PipelineCache::GetOrCreatePipeline(PipelineKey const& key, vk::RenderPass compatibility_render_pass)
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
    bool is_compute = false;
    if (resource_system_) {
        auto const* shader = resource_system_->GetShaderManager().GetShader(key.shader_id);
        if (shader) {
            if (shader->compute_shader && shader->compute_shader->IsInitialized()) {
                pipeline_info.shader_stages.push_back(shader->compute_shader->GetPipelineStageInfo());
                is_compute = true;
                pipeline_info.is_compute = true;
            } else {
                if (shader->vertex_shader && shader->vertex_shader->IsInitialized()) {
                    pipeline_info.shader_stages.push_back(shader->vertex_shader->GetPipelineStageInfo());
                }
                if (shader->fragment_shader && shader->fragment_shader->IsInitialized()) {
                    pipeline_info.shader_stages.push_back(shader->fragment_shader->GetPipelineStageInfo());
                }
            }
        }
    }
    
    if (!is_compute) {
        bool const depth_only_pipeline =
            (key.rt_format == 0) && (key.depth_format != 0 || key.stencil_format != 0);
        bool const alpha_blended_pipeline = key.render_state_id == 2;

        // Set vertex input state (simplified for now)
        vkfw::PipelineVertexInputState vertex_input;
        vertex_input.topology = vk::PrimitiveTopology::eTriangleList;
        if (key.vertex_layout_id == 2) {
            // Lightweight 2D UI Vertex Format: UiVertex with texture_index
            vertex_input.vertex_inputs = {
                // 0. position (glm::vec2) -> location 0
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 0,
                    .stride = sizeof(ave::render::UiVertex),
                    .format = vk::Format::eR32G32Sfloat,
                    .offset = offsetof(ave::render::UiVertex, position),
                },
                // 1. uv (glm::vec2) -> location 1
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 1,
                    .stride = sizeof(ave::render::UiVertex),
                    .format = vk::Format::eR32G32Sfloat,
                    .offset = offsetof(ave::render::UiVertex, uv),
                },
                // 2. color (glm::vec4) -> location 2
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 2,
                    .stride = sizeof(ave::render::UiVertex),
                    .format = vk::Format::eR32G32B32A32Sfloat,
                    .offset = offsetof(ave::render::UiVertex, color),
                },
                // 3. texture_index (uint32_t) -> location 3
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 3,
                    .stride = sizeof(ave::render::UiVertex),
                    .format = vk::Format::eR32Uint,
                    .offset = offsetof(ave::render::UiVertex, texture_index),
                },
            };
        } else {
            // SharedDataContract::VertexData: position + color + texcoord0.
            vertex_input.vertex_inputs = {
                // 0. position (glm::vec3)
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 0,
                    .stride = sizeof(ave::project::VertexData),
                    .format = vk::Format::eR32G32B32Sfloat,
                    .offset = offsetof(ave::project::VertexData, position),
                },
                // 1. normal (glm::vec3)
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 1,
                    .stride = sizeof(ave::project::VertexData),
                    .format = vk::Format::eR32G32B32Sfloat,
                    .offset = offsetof(ave::project::VertexData, normal),
                },
                // 2. tangent (glm::vec4)
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 2,
                    .stride = sizeof(ave::project::VertexData),
                    .format = vk::Format::eR32G32B32A32Sfloat,
                    .offset = offsetof(ave::project::VertexData, tangent),
                },
                // 3. texcoord0 (glm::vec2)
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 3,
                    .stride = sizeof(ave::project::VertexData),
                    .format = vk::Format::eR32G32Sfloat,
                    .offset = offsetof(ave::project::VertexData, texcoord0),
                },
                // 4. texcoord1 (glm::vec2)
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 4,
                    .stride = sizeof(ave::project::VertexData),
                    .format = vk::Format::eR32G32Sfloat,
                    .offset = offsetof(ave::project::VertexData, texcoord1),
                },
                // 5. color (glm::vec4)
                vkfw::PipelineVertexInput{
                    .binding = 0,
                    .location = 5,
                    .stride = sizeof(ave::project::VertexData),
                    .format = vk::Format::eR32G32B32A32Sfloat,
                    .offset = offsetof(ave::project::VertexData, color),
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

        pipeline_info.depth_stencil.depth_test_enable = depth_only_pipeline;
        pipeline_info.depth_stencil.depth_write_enable = depth_only_pipeline;
        pipeline_info.depth_stencil.depth_compare_op = vk::CompareOp::eLessOrEqual;

        if (!depth_only_pipeline) {
            vkfw::PipelineColorBlendAttachment blend_attachment{};
            if (alpha_blended_pipeline) {
                blend_attachment.blend_enable = true;
                blend_attachment.src_color_blend = vk::BlendFactor::eSrcAlpha;
                blend_attachment.dst_color_blend = vk::BlendFactor::eOneMinusSrcAlpha;
                blend_attachment.color_blend_op = vk::BlendOp::eAdd;
                blend_attachment.src_alpha_blend = vk::BlendFactor::eOne;
                blend_attachment.dst_alpha_blend = vk::BlendFactor::eOneMinusSrcAlpha;
                blend_attachment.alpha_blend_op = vk::BlendOp::eAdd;
            }
            pipeline_info.color_blend.attachments = {blend_attachment};
        }
    }
    
    // Set pipeline layout (fixed engine convention; caller chooses a profile via PipelineKey::layout_profile)
    if (pipeline_layout_cache_) {
        PipelineLayoutKey layout_key;

        uint32_t frame_set_layout_id = 0;
        uint32_t material_set_layout_id = 0;
        uint32_t object_set_layout_id = 0;
        uint32_t compute_set_layout_id = 0;
        uint32_t texture_set_layout_id = 0;

        if (auto* desc_cache = pipeline_layout_cache_->GetDescriptorSetLayoutCache()) {
            if (is_compute) {
                if (key.layout_profile == PipelineLayoutProfile::ComputeCulling_Set0_Only) {
                    DescriptorSetLayoutKey culling_set_key;
                    culling_set_key.bindings = {
                        DescriptorBinding{
                            .binding = 0,
                            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::StorageBuffer),
                            .descriptor_count = 1,
                            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eCompute),
                        },
                        DescriptorBinding{
                            .binding = 1,
                            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::StorageBuffer),
                            .descriptor_count = 1,
                            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eCompute),
                        }
                    };
                    compute_set_layout_id = desc_cache->GetOrCreateLayout(culling_set_key);
                }
            } else {
                if (key.layout_profile == PipelineLayoutProfile::Full_Set0_Set1_Set2 
                    || key.layout_profile == PipelineLayoutProfile::Material_Set0_Set1 
                    || key.layout_profile == PipelineLayoutProfile::Global_Set0_Only) {
                    frame_set_layout_id = desc_cache->GetOrCreateLayout(MakeFrameSetLayoutKey());
                }
                if (key.layout_profile == PipelineLayoutProfile::Full_Set0_Set1_Set2 
                    || key.layout_profile == PipelineLayoutProfile::Material_Set0_Set1) {
                    material_set_layout_id = desc_cache->GetOrCreateLayout(MakeMaterialSetLayoutKey());
                }
                if (key.layout_profile == PipelineLayoutProfile::Full_Set0_Set1_Set2) {
                    object_set_layout_id = desc_cache->GetOrCreateLayout(MakeObjectSetLayoutKey());
                }
                if (key.layout_profile == PipelineLayoutProfile::Texture_Set0_Only) {
                    texture_set_layout_id = desc_cache->GetOrCreateLayout(MakeTextureSetLayoutKey());
                }
            }
        }

        if (compute_set_layout_id != 0) {
            layout_key.set_layout_ids.push_back(compute_set_layout_id);
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
        if (texture_set_layout_id != 0) {
            layout_key.set_layout_ids.push_back(texture_set_layout_id);
        }

        std::vector<vk::PushConstantRange> push_constants;
        if (is_compute && key.layout_profile == PipelineLayoutProfile::ComputeCulling_Set0_Only) {
            vk::PushConstantRange range{};
            range.stageFlags = vk::ShaderStageFlagBits::eCompute;
            range.offset = 0;
            range.size = 112; // 6 planes (6 * 16 = 96 bytes) + total_instances (4 bytes) + 12 bytes padding = 112 bytes
            push_constants.push_back(range);
        } else if (!is_compute &&
                   (key.layout_profile == PipelineLayoutProfile::Global_Set0_Only ||
                    key.layout_profile == PipelineLayoutProfile::Material_Set0_Set1 ||
                    key.layout_profile == PipelineLayoutProfile::Full_Set0_Set1_Set2)) {
            vk::PushConstantRange range{};
            range.stageFlags = vk::ShaderStageFlagBits::eVertex;
            range.offset = 0;
            range.size = sizeof(glm::mat4);
            push_constants.push_back(range);
        }
        uint32_t layout_id = pipeline_layout_cache_->GetOrCreateLayout(layout_key, push_constants);
        auto const* layout = pipeline_layout_cache_->GetLayout(layout_id);
        if (layout) {
            pipeline_info.layout = layout->Handle();
        }
    }
    
    if (!is_compute) {
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

        if (ctx_ && ctx_->SupportsDynamicRendering()) {
            pipeline_info.use_dynamic_rendering = true;
            pipeline_info.color_formats = {static_cast<vk::Format>(key.rt_format)};
            pipeline_info.depth_format = static_cast<vk::Format>(key.depth_format);
            pipeline_info.stencil_format = static_cast<vk::Format>(key.stencil_format);
        } else {
            pipeline_info.use_dynamic_rendering = false;
            pipeline_info.render_pass = compatibility_render_pass;
        }
    }
    
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
        std::vector<vkfw::DescriptorBinding> pool_bindings;
        pool_bindings.push_back(vkfw::DescriptorBinding{
            .binding = 0,
            .type = vkfw::DescriptorType::UniformBuffer,
            .descriptor_count = 1,
            .stage_flags = vk::ShaderStageFlagBits::eAll,
        });
        pool_bindings.push_back(vkfw::DescriptorBinding{
            .binding = 0,
            .type = vkfw::DescriptorType::CombinedImageSampler,
            .descriptor_count = 1,
            .stage_flags = vk::ShaderStageFlagBits::eAll,
        });
        pool_bindings.push_back(vkfw::DescriptorBinding{
            .binding = 0,
            .type = vkfw::DescriptorType::StorageBuffer,
            .descriptor_count = 1,
            .stage_flags = vk::ShaderStageFlagBits::eAll,
        });

        pool_ = std::make_unique<vkfw::VkDescriptorPool>();
        pool_->Init(*ctx_, pool_bindings, 1000);
    }
    
    if (desc_set_layout_cache_) {
        if (auto const* layout = desc_set_layout_cache_->GetLayout(layout_id)) {
            vk::DescriptorSetLayout const layout_handle = layout->Handle();
            try {
                auto sets = pool_->AllocateDescriptorSets(*ctx_, 1, &layout_handle);
                if (sets.empty()) {
                    return 0;
                }
                sets_.emplace(id, std::move(sets[0]));
            } catch (...) {
                return 0;
            }
        }
    }
    
    return id;
}

void DescriptorAllocator::FreeDescriptorSet(uint32_t set_id)
{
    sets_.erase(set_id);
    free_sets_.push_back(set_id);
}

vk::DescriptorSet DescriptorAllocator::GetHandle(uint32_t set_id) const
{
    auto it = sets_.find(set_id);
    if (it == sets_.end()) {
        return {};
    }
    return *it->second;
}

bool DescriptorAllocator::UpdateUniformBuffer(uint32_t set_id,
                                             uint32_t binding,
                                             vk::Buffer buffer,
                                             vk::DeviceSize offset,
                                             vk::DeviceSize range)
{
    if (!ctx_) {
        return false;
    }

    auto it = sets_.find(set_id);
    if (it == sets_.end()) {
        return false;
    }

    vk::DescriptorBufferInfo buf{};
    buf.buffer = buffer;
    buf.offset = offset;
    buf.range = range;

    vk::WriteDescriptorSet write{};
    write.dstSet = *it->second;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eUniformBuffer;
    write.pBufferInfo = &buf;

    ctx_->Device().updateDescriptorSets(write, {});
    return true;
}

bool DescriptorAllocator::UpdateImageSampler(uint32_t set_id,
                            uint32_t binding,
                            vk::Sampler sampler,
                            vk::ImageView image_view,
                            vk::ImageLayout image_layout){
    if (!ctx_) {
        return false;
    }

    auto it = sets_.find(set_id);
    if (it == sets_.end()) {
        return false;
    }

    // 1. 核心区别：Buffer 换成 ImageInfo
    vk::DescriptorImageInfo img_info{};
    img_info.sampler = sampler;               // 传入的采样器
    img_info.imageView = image_view;           // 传入的图片视图
    img_info.imageLayout = image_layout;       // 通常是 vk::ImageLayout::eShaderReadOnlyOptimal

    // 2. 配置写入结构体
    vk::WriteDescriptorSet write{};
    write.dstSet = *it->second;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    
    // 3. 核心区别：类型换成 eCombinedImageSampler
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &img_info;              // 注意：这里是 pImageInfo，而不是 pBufferInfo

    // 4. 提交给 Vulkan 设备更新
    ctx_->Device().updateDescriptorSets(write, {});
    return true;
}

bool DescriptorAllocator::UpdateImageSamplerArray(uint32_t set_id,
                            uint32_t binding,
                            uint32_t array_index,
                            vk::Sampler sampler,
                            vk::ImageView image_view,
                            vk::ImageLayout image_layout){
    if (!ctx_) {
        return false;
    }

    auto it = sets_.find(set_id);
    if (it == sets_.end()) {
        return false;
    }

    vk::DescriptorImageInfo img_info{};
    img_info.sampler = sampler;
    img_info.imageView = image_view;
    img_info.imageLayout = image_layout;

    vk::WriteDescriptorSet write{};
    write.dstSet = *it->second;
    write.dstBinding = binding;
    write.dstArrayElement = array_index;
    write.descriptorCount = 1;
    
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.pImageInfo = &img_info;

    ctx_->Device().updateDescriptorSets(write, {});
    return true;
}

bool DescriptorAllocator::UpdateStorageBuffer(uint32_t set_id,
                                             uint32_t binding,
                                             vk::Buffer buffer,
                                             vk::DeviceSize offset,
                                             vk::DeviceSize range)
{
    if (!ctx_) {
        return false;
    }

    auto it = sets_.find(set_id);
    if (it == sets_.end()) {
        return false;
    }

    vk::DescriptorBufferInfo buf{};
    buf.buffer = buffer;
    buf.offset = offset;
    buf.range = range;

    vk::WriteDescriptorSet write{};
    write.dstSet = *it->second;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vk::DescriptorType::eStorageBuffer;
    write.pBufferInfo = &buf;

    ctx_->Device().updateDescriptorSets(write, {});
    return true;
}

void DescriptorAllocator::Clear()
{
    free_sets_.clear();
    sets_.clear();
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

}// namespace ave::render
