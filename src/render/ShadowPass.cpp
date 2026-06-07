#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {

void ShadowPass::Reset(vkfw::VkContext* ctx)
{
    shadow_view_projection_ = glm::mat4{1.0f};
    if (ctx != nullptr) {
        for (auto& binding : frame_bindings_) {
            if (binding.ubo.IsInitialized()) {
                binding.ubo.Shutdown(*ctx);
            }
        }
        for (auto& [material_id, bindings] : material_bindings_) {
            (void)material_id;
            for (auto& binding : bindings) {
                if (binding.ubo.IsInitialized()) {
                    binding.ubo.Shutdown(*ctx);
                }
            }
        }
        if (fallback_white_texture_.IsInitialized()) {
            fallback_white_texture_.Shutdown(*ctx);
        }
        for (auto& shadow_map : shadow_maps_) {
            if (shadow_map.IsInitialized()) {
                shadow_map.Shutdown(*ctx);
            }
        }
    }
    frame_bindings_.clear();
    material_bindings_.clear();
    fallback_white_texture_ = {};
    shadow_maps_.clear();
    shadow_map_initialized_.clear();
}

void ShadowPass::Preload(RenderPassContext const& context)
{
    if (context.resources == nullptr) {
        return;
    }

    auto& shader_mgr = context.resources->GetShaderManager();
    if (shadow_shader_id_ == 0) {
        shadow_shader_id_ = shader_mgr.LoadShader("compiled_shaders/shadow_depth");
    }
}

PassDataFilter ShadowPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::Shadow;
    filter.opaque_only = true;
    filter.shadow_casters_only = true;
    return filter;
}

void ShadowPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    using namespace detail;

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();
    auto& texture_mgr = context.resources->GetTextureManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();
    uint32_t const image_count = context.frame_resource_count != 0 ? context.frame_resource_count : 1u;
    uint32_t const image_index = context.frame_resource_index;
    if (image_count == 0 || image_index >= image_count) {
        return;
    }
    if (frame_bindings_.size() != image_count) {
        frame_bindings_.resize(image_count);
    }
    if (shadow_maps_.size() != image_count) {
        shadow_maps_.resize(image_count);
    }
    if (shadow_map_initialized_.size() != image_count) {
        shadow_map_initialized_.assign(image_count, 0u);
    }
    auto& frame_binding = frame_bindings_[image_index];
    auto& shadow_map = shadow_maps_[image_index];

    struct ShadowFrameUbo {
        glm::mat4 shadow_view_projection{1.0f};
    };

    struct ObjectPushConstants {
        glm::mat4 world{1.0f};
    };

    if (!has_vk) {
        return;
    }

    EnsureFallbackWhiteTexture(*context.vk, fallback_white_texture_);

    if (shadow_shader_id_ == 0) {
        shadow_shader_id_ = shader_mgr.LoadShader("compiled_shaders/shadow_depth");
        if (shadow_shader_id_ == 0) {
            LOGE("ShadowPass failed to load shadow shader");
            return;
        }
    }

    if (!shadow_map.IsInitialized()) {
        if (!shadow_map.Init(*context.vk, vkfw::TextureInfo{
                                               .width = kShadowMapSize,
                                               .height = kShadowMapSize,
                                               .mip_levels = 1,
                                               .format = vkfw::TextureFormat::D32_SFLOAT,
                                               .usage = static_cast<vkfw::TextureUsage>(
                                                   static_cast<uint32_t>(vkfw::TextureUsage::DepthStencilAttachment) |
                                                   static_cast<uint32_t>(vkfw::TextureUsage::Sampled)),
                                               .mipmap = false,
                                           })) {
            LOGE("ShadowPass failed to create shadow map");
            return;
        }
        shadow_map_initialized_[image_index] = 0u;
    }

    shadow_view_projection_ = BuildShadowViewProjection(view, context.frame);

    ShadowFrameUbo frame_ubo{};
    frame_ubo.shadow_view_projection = shadow_view_projection_;

    if (!frame_binding.ubo.IsInitialized()) {
        frame_binding.ubo.Init(*context.vk, vkfw::BufferInfo{
                                               .size = static_cast<uint32_t>(sizeof(ShadowFrameUbo)),
                                               .usage = vkfw::BufferUsage::Uniform,
                                               .mappable = true,
                                           });
    }
    frame_binding.ubo.UpdateData(*context.vk, &frame_ubo, static_cast<uint32_t>(sizeof(ShadowFrameUbo)));

    if (frame_binding.descriptor_set_id == 0) {
        uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
        frame_binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(frame_layout_id);
    }
    if (frame_binding.descriptor_set_id != 0) {
        desc_alloc.UpdateUniformBuffer(frame_binding.descriptor_set_id, 0, frame_binding.ubo.Handle(), 0, sizeof(ShadowFrameUbo));
    }

    vk::ImageLayout const old_layout = shadow_map_initialized_[image_index] != 0u
        ? vk::ImageLayout::eShaderReadOnlyOptimal
        : vk::ImageLayout::eUndefined;
    TransitionImageLayout(context.command_buffer,
                          shadow_map.Handle(),
                          vk::ImageAspectFlagBits::eDepth,
                          old_layout,
                          vk::ImageLayout::eDepthAttachmentOptimal,
                          {},
                          vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                          vk::PipelineStageFlagBits::eTopOfPipe,
                          vk::PipelineStageFlagBits::eEarlyFragmentTests);

    vk::ClearDepthStencilValue clear_depth{};
    clear_depth.depth = 1.0f;
    clear_depth.stencil = 0;
    if (!BeginShadowMapRendering(context, shadow_map, kShadowMapSize, clear_depth)) {
        LOGE("ShadowPass failed to begin shadow-map rendering");
        return;
    }

    for (auto const* renderable : view.renderables) {
        if (!renderable) {
            continue;
        }

        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            continue;
        }

        auto const* material = renderable->material_handle != 0
            ? mat_mgr.GetMaterial(renderable->material_handle)
            : mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            continue;
        }

        PipelineKey key = MakePipelineKey(shadow_shader_id_, *mesh);
        key.layout_profile = PipelineLayoutProfile::Material_Set0_Set1;
        key.rt_format = 0;
        key.depth_format = static_cast<uint32_t>(vk::Format::eD32Sfloat);
        key.viewport_width = kShadowMapSize;
        key.viewport_height = kShadowMapSize;

        vk::RenderPass active_render_pass = context.vk->SupportsDynamicRendering()
            ? vk::RenderPass{}
            : **g_compatibility_shadow_render_pass;
        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, active_render_pass);
        if (pipeline_id == 0) {
            continue;
        }

        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (!pipeline) {
            continue;
        }

        auto& material_binding_list = material_bindings_[material->id];
        if (material_binding_list.size() != image_count) {
            material_binding_list.resize(image_count);
        }
        auto& material_binding = material_binding_list[image_index];
        if (!material_binding.ubo.IsInitialized()) {
            material_binding.ubo.Init(*context.vk, vkfw::BufferInfo{
                                                       .size = static_cast<uint32_t>(sizeof(glm::vec4) * 2u),
                                                       .usage = vkfw::BufferUsage::Uniform,
                                                       .mappable = true,
                                                   });
        }
        if (material_binding.descriptor_set_id == 0) {
            uint32_t const material_layout_id = desc_cache.GetOrCreateLayout(MakeMaterialSetLayoutKey());
            material_binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(material_layout_id);
        }

        struct MaterialUbo {
            glm::vec4 base_color{1.0f};
            glm::vec4 params{0.0f};
        } material_ubo{};
        material_ubo.base_color = renderable->has_color_override ? renderable->color_override : material->base_color;
        material_ubo.params = glm::vec4(
            material->metallic,
            material->roughness,
            renderable->receives_shadow ? 1.0f : 0.0f,
            material->normal_scale);
        material_binding.ubo.UpdateData(*context.vk, &material_ubo, static_cast<uint32_t>(sizeof(MaterialUbo)));

        if (material_binding.descriptor_set_id != 0) {
            desc_alloc.UpdateUniformBuffer(material_binding.descriptor_set_id,
                                           0,
                                           material_binding.ubo.Handle(),
                                           0,
                                           sizeof(MaterialUbo));
            vk::Sampler const sampler = GetCommonSampler(*context.vk);
            if (auto const* base_color_texture =
                    ResolveTextureOrFallback(*context.vk, texture_mgr, material->base_color_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                              1,
                                              sampler,
                                              base_color_texture->View(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }

        context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

        ObjectPushConstants object_push{};
        object_push.world = renderable->world;
        context.command_buffer.pushConstants(pipeline->Layout(),
                                             vk::ShaderStageFlagBits::eVertex,
                                             0,
                                             sizeof(ObjectPushConstants),
                                             &object_push);

        if (frame_binding.descriptor_set_id != 0) {
            vk::DescriptorSet const frame_set = desc_alloc.GetHandle(frame_binding.descriptor_set_id);
            if (frame_set) {
                context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                          pipeline->Layout(),
                                                          0,
                                                          1,
                                                          &frame_set,
                                                          0,
                                                          nullptr);
            }
        }
        if (material_binding.descriptor_set_id != 0) {
            vk::DescriptorSet const material_set = desc_alloc.GetHandle(material_binding.descriptor_set_id);
            if (material_set) {
                context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                          pipeline->Layout(),
                                                          1,
                                                          1,
                                                          &material_set,
                                                          0,
                                                          nullptr);
            }
        }

        vk::DeviceSize offset = 0;
        context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
        if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
            context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
            uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
            context.command_buffer.drawIndexed(index_count, 1, renderable->first_index, static_cast<int32_t>(renderable->first_vertex), 0);
        } else {
            uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
            context.command_buffer.draw(vertex_count, 1, renderable->first_vertex, 0);
        }
    }

    EndShadowMapRendering(context);
    TransitionImageLayout(context.command_buffer,
                          shadow_map.Handle(),
                          vk::ImageAspectFlagBits::eDepth,
                          vk::ImageLayout::eDepthAttachmentOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                          vk::AccessFlagBits::eShaderRead,
                          vk::PipelineStageFlagBits::eLateFragmentTests,
                          vk::PipelineStageFlagBits::eFragmentShader);
    if (context.frame_graph_resources != nullptr) {
        context.frame_graph_resources->SetTexture(FrameGraphResourceRegistry::ShadowMap, &shadow_map);
        context.frame_graph_resources->SetMatrix(FrameGraphResourceRegistry::ShadowViewProjection, shadow_view_projection_);
    }
    shadow_map_initialized_[image_index] = 1u;
}

} // namespace ave::render
