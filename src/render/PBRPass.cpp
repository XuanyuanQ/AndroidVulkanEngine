#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {


void PBRPass::Reset(vkfw::VkContext* ctx)
{
    fallback_material_id_ = 0;
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
        if (fallback_normal_texture_.IsInitialized()) {
            fallback_normal_texture_.Shutdown(*ctx);
        }
        for (auto& depth_stencil : depth_stencils_) {
            if (depth_stencil.IsInitialized()) {
                depth_stencil.Shutdown(*ctx);
            }
        }
    }
    frame_bindings_.clear();
    material_bindings_.clear();
    depth_stencils_.clear();
}

void PBRPass::EnsureEnvironmentMaps(vkfw::VkContext& ctx,
                                    resource::ResourceSystem* resources,
                                    glm::vec4 const& clear_color,
                                    glm::vec3 const& ambient_color)
{
    detail::EnsureSharedEnvironmentMaps(ctx, resources, clear_color, ambient_color);
}

void PBRPass::Preload(RenderPassContext const& context)
{
    if (context.vk == nullptr || context.resources == nullptr || context.frame == nullptr) {
        return;
    }

    detail::EnsureFallbackWhiteTexture(*context.vk, fallback_white_texture_);
    detail::EnsureFallbackNormalTexture(*context.vk, fallback_normal_texture_);

    glm::vec4 const clear_color = context.frame->environment.clear_color;
    glm::vec3 const ambient_color = context.frame->environment.ambient_color;
    EnsureEnvironmentMaps(*context.vk, context.resources, clear_color, ambient_color);
}

PassDataFilter PBRPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ForwardOpaque;
    filter.layer_mask = 0xFFFFFFFFu;
    return filter;
}

void PBRPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    using namespace detail;

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.color_target.IsValid() && context.command_buffer != vk::CommandBuffer{};

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
    if (depth_stencils_.size() != image_count) {
        depth_stencils_.resize(image_count);
    }
    auto& frame_binding = frame_bindings_[image_index];
    auto& fallback_depth_stencil = depth_stencils_[image_index];

    struct MaterialUbo {
        glm::vec4 base_color{1.0f};
        glm::vec4 params{0.0f};
    };

    struct ObjectPushConstants {
        glm::mat4 world{1.0f};
    };

    bool began_rendering = false;
    if (has_vk) {
        EnsureFallbackNormalTexture(*context.vk, fallback_normal_texture_);
        EnsureFallbackWhiteTexture(*context.vk, fallback_white_texture_);
        uint32_t const width = context.color_target.extent.width;
        uint32_t const height = context.color_target.extent.height;

        vkfw::VkTexture* depth_target = context.depth_target.texture;
        if (depth_target == nullptr || !depth_target->IsInitialized()) {
            if (fallback_depth_stencil.IsInitialized()) {
                auto const extent = fallback_depth_stencil.Extent();
                if (extent.width != width || extent.height != height) {
                    fallback_depth_stencil.Shutdown(*context.vk);
                }
            }

            if (!fallback_depth_stencil.IsInitialized()) {
                if (!fallback_depth_stencil.Init(*context.vk, vkfw::TextureInfo{
                                                       .width = width,
                                                       .height = height,
                                                       .mip_levels = 1,
                                                       .format = vkfw::TextureFormat::D32_SFLOAT,
                                                       .usage = vkfw::TextureUsage::DepthStencilAttachment,
                                                       .mipmap = false,
                                                   })) {
                    LOGE("PBRPass failed to create depth stencil texture");
                    return;
                }
            }

            depth_target = &fallback_depth_stencil;
            context.depth_target.texture = depth_target;
        }

        vk::ClearValue clear{};
        clear.color.float32[0] = context.frame != nullptr ? context.frame->environment.clear_color.x : 0.03f;
        clear.color.float32[1] = context.frame != nullptr ? context.frame->environment.clear_color.y : 0.04f;
        clear.color.float32[2] = context.frame != nullptr ? context.frame->environment.clear_color.z : 0.06f;
        clear.color.float32[3] = context.frame != nullptr ? context.frame->environment.clear_color.w : 1.0f;
        bool const clear_depth = depth_target == &fallback_depth_stencil;
        began_rendering = BeginRenderTargetRendering(context, clear, false, depth_target, clear_depth);
        if (!began_rendering) {
            LOGE("PBRPass failed to begin rendering");
            return;
        }
    }

    if (has_vk) {
        FrameUbo frame_ubo{};
        core::FrameViewData const* frame_view = CurrentFrameView(context);
        if (context.frame != nullptr) {
            if (frame_view != nullptr) {
                frame_ubo.view_projection = frame_view->view_projection;
            }
            if (context.frame_graph_resources != nullptr) {
                frame_ubo.shadow_view_projection = context.frame_graph_resources->GetMatrix(
                    FrameGraphResourceRegistry::ShadowViewProjection,
                    frame_ubo.shadow_view_projection);
            }
            if (frame_view != nullptr) {
                frame_ubo.camera_position = glm::vec4(frame_view->world_position, 1.0f);
            }
            frame_ubo.ambient_color = glm::vec4(context.frame->environment.ambient_color, 1.0f);
            frame_ubo.clear_color = context.frame->environment.clear_color;
            if (frame_view != nullptr) {
                frame_ubo.view = frame_view->view;
                frame_ubo.projection = frame_view->projection;
            }
        }

        if (!view.lights.empty() && view.lights.front() != nullptr) {
            auto const& light = *view.lights.front();
            frame_ubo.light_position_range = glm::vec4(light.position, light.range);
            frame_ubo.light_direction_type = glm::vec4(light.direction, light.type == "directional" ? 0.0f : 1.0f);
            frame_ubo.light_color_intensity = glm::vec4(light.color, light.intensity);
        }

        if (!frame_binding.ubo.IsInitialized()) {
            frame_binding.ubo.Init(*context.vk, vkfw::BufferInfo{
                                                    .size = static_cast<uint32_t>(sizeof(FrameUbo)),
                                                    .usage = vkfw::BufferUsage::Uniform,
                                                    .mappable = true,
                                                });
        }
        frame_binding.ubo.UpdateData(*context.vk, &frame_ubo, static_cast<uint32_t>(sizeof(FrameUbo)));

        glm::vec4 const clear_color = context.frame != nullptr
            ? context.frame->environment.clear_color
            : frame_ubo.clear_color;
        glm::vec3 const ambient_color = context.frame != nullptr
            ? context.frame->environment.ambient_color
            : glm::vec3{frame_ubo.ambient_color.x, frame_ubo.ambient_color.y, frame_ubo.ambient_color.z};
        EnsureEnvironmentMaps(*context.vk, context.resources, clear_color, ambient_color);

        if (frame_binding.descriptor_set_id == 0) {
            uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
            frame_binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(frame_layout_id);
        }
        if (frame_binding.descriptor_set_id != 0) {
            desc_alloc.UpdateUniformBuffer(frame_binding.descriptor_set_id, 0, frame_binding.ubo.Handle(), 0, sizeof(FrameUbo));
            vkfw::VkTexture* shadow_map = context.frame_graph_resources != nullptr
                ? context.frame_graph_resources->GetTexture(FrameGraphResourceRegistry::ShadowMap)
                : nullptr;
            if (shadow_map) {
                vk::Sampler sampler = GetShadowSampler(*context.vk);
                desc_alloc.UpdateImageSampler(frame_binding.descriptor_set_id, 1, sampler, shadow_map->View(), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            vk::Sampler const sampler = GetCommonSampler(*context.vk);
            if (g_shared_environment_maps.environment_cubemap.IsInitialized()) {
                desc_alloc.UpdateImageSampler(frame_binding.descriptor_set_id, 2, sampler, g_shared_environment_maps.environment_cubemap.View(), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (g_shared_environment_maps.irradiance_cubemap.IsInitialized()) {
                desc_alloc.UpdateImageSampler(frame_binding.descriptor_set_id, 3, sampler, g_shared_environment_maps.irradiance_cubemap.View(), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (g_shared_environment_maps.prefilter_cubemap.IsInitialized()) {
                desc_alloc.UpdateImageSampler(frame_binding.descriptor_set_id, 4, sampler, g_shared_environment_maps.prefilter_cubemap.View(), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (g_shared_environment_maps.brdf_lut.IsInitialized()) {
                desc_alloc.UpdateImageSampler(frame_binding.descriptor_set_id, 5, sampler, g_shared_environment_maps.brdf_lut.View(), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }
    }
    uint32_t renderable_index = 0;
    for (auto const* renderable : view.renderables) {
        uint32_t const culling_index = renderable_index++;
        if (!has_vk) {
            continue;
        }
        if (!renderable) {
            continue;
        }

        if (false && culling_index < g_culling_visibility.size() && g_culling_visibility[culling_index] == 0) {
            continue;
        }
        auto const* material = renderable->material_handle != 0
            ? mat_mgr.GetMaterial(renderable->material_handle)
            : mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            if (fallback_material_id_ == 0) {
                uint32_t fallback_shader_id = shader_mgr.LoadShader("compiled_shaders/default_pbr");
                if (fallback_shader_id != 0) {
                    fallback_material_id_ = mat_mgr.CreateMaterial("__fallback/default_material__", fallback_shader_id);
                    mat_mgr.SetBaseColor(fallback_material_id_, glm::vec4{0.85f, 0.85f, 0.88f, 1.0f});
                }
            }
            if (fallback_material_id_ != 0) {
                material = mat_mgr.GetMaterial(fallback_material_id_);
            }
        }
        if (!material) {
            continue;
        }

        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            continue;
        }

        auto const* shader = material->shader_id != 0 ? shader_mgr.GetShader(material->shader_id) : nullptr;
        if (!shader) {
            continue;
        }

        PipelineKey key = MakePipelineKey(shader->id, *mesh);
        key.layout_profile = PipelineLayoutProfile::Material_Set0_Set1;
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.color_target.format);
            key.depth_format = static_cast<uint32_t>(context.depth_target.format);
            key.viewport_width = context.color_target.extent.width;
            key.viewport_height = context.color_target.extent.height;
        }

        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(
                key,
                context.color_target.compatibility_render_pass);
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
                                                       .size = static_cast<uint32_t>(sizeof(MaterialUbo)),
                                                       .usage = vkfw::BufferUsage::Uniform,
                                                       .mappable = true,
                                                   });
        }
        if (material_binding.descriptor_set_id == 0) {
            uint32_t const material_layout_id = desc_cache.GetOrCreateLayout(MakeMaterialSetLayoutKey());
            material_binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(material_layout_id);
        }

        MaterialUbo material_ubo{};
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
            if (auto const* normal_texture =
                    ResolveNormalTextureOrFallback(*context.vk, texture_mgr, material->normal_texture, fallback_normal_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                              2,
                                              sampler,
                                              normal_texture->View(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (auto const* mr_texture =
                     ResolveTextureOrFallback(*context.vk, texture_mgr, material->metallic_roughness_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                                3,
                                                sampler,
                                                mr_texture->View(),
                                                vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (auto const* alpha_mask_texture =
                    ResolveTextureOrFallback(*context.vk, texture_mgr, material->alpha_mask_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                                4,
                                                sampler,
                                                alpha_mask_texture->View(),
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

        vk::DescriptorSet sets[2]{};
        uint32_t set_count = 0;
        if (frame_binding.descriptor_set_id != 0) {
            vk::DescriptorSet const frame_set = desc_alloc.GetHandle(frame_binding.descriptor_set_id);
            if (frame_set) {
                sets[set_count++] = frame_set;
            }
        }
        if (material_binding.descriptor_set_id != 0) {
            vk::DescriptorSet const material_set = desc_alloc.GetHandle(material_binding.descriptor_set_id);
            if (material_set) {
                sets[set_count++] = material_set;
            }
        }
        if (set_count > 0) {
            context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                      pipeline->Layout(),
                                                      0,
                                                      set_count,
                                                      sets,
                                                      0,
                                                      nullptr);
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

    if (began_rendering) {
        EndSwapchainRendering(context);
    }
}

} // namespace ave::render
