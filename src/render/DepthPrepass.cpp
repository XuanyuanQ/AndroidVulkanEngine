#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {

void DepthPrepass::Reset(vkfw::VkContext* ctx)
{
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
        for (auto& texture : depth_textures_) {
            if (texture.IsInitialized()) {
                texture.Shutdown(*ctx);
            }
        }
        if (fallback_white_texture_.IsInitialized()) {
            fallback_white_texture_.Shutdown(*ctx);
        }
        if (fallback_normal_texture_.IsInitialized()) {
            fallback_normal_texture_.Shutdown(*ctx);
        }
    }
    frame_bindings_.clear();
    material_bindings_.clear();
    depth_textures_.clear();
    depth_texture_ready_.clear();
    depth_shader_id_ = 0;
}

void DepthPrepass::Preload(RenderPassContext const& context)
{
    if (context.resources == nullptr) {
        return;
    }

    auto& shader_mgr = context.resources->GetShaderManager();
    if (depth_shader_id_ == 0) {
        depth_shader_id_ = shader_mgr.LoadShader("compiled_shaders/shadow_depth");
    }
}

PassDataFilter DepthPrepass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::DepthPrepass;
    filter.opaque_only = true;
    return filter;
}

void DepthPrepass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    using namespace detail;

    if (context.resources == nullptr || context.pipelines == nullptr || context.vk == nullptr ||
        context.command_buffer == vk::CommandBuffer{} || !context.color_target.IsValid()) {
        return;
    }

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();
    auto& texture_mgr = context.resources->GetTextureManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();
    uint32_t const image_index = context.frame_resource_index;
    uint32_t const image_count = context.frame_resource_count != 0 ? context.frame_resource_count : 1u;
    if (image_count == 0 || image_index >= image_count) {
        return;
    }
    if (frame_bindings_.size() != image_count) {
        frame_bindings_.resize(image_count);
    }
    if (material_bindings_.size() == 0) {
        material_bindings_.reserve(64);
    }
    if (depth_texture_ready_.size() != image_count) {
        depth_texture_ready_.assign(image_count, 0u);
    }
    auto& frame_binding = frame_bindings_[image_index];
    if (context.depth_target.texture == nullptr) {
        if (depth_textures_.size() != image_count) {
            depth_textures_.resize(image_count);
        }
        context.depth_target.texture = &depth_textures_[image_index];
    }
    if (context.depth_target.ready == nullptr) {
        context.depth_target.ready = &depth_texture_ready_[image_index];
    }
    auto& depth_texture = *context.depth_target.texture;
    auto& depth_texture_ready = *context.depth_target.ready;

    struct DepthFrameUbo {
        glm::mat4 view_projection{1.0f};
    };

    struct ObjectPushConstants {
        glm::mat4 world{1.0f};
    };

    struct MaterialUbo {
        glm::vec4 base_color{1.0f};
        glm::vec4 params{0.0f};
    };

    EnsureFallbackWhiteTexture(*context.vk, fallback_white_texture_);
    EnsureFallbackNormalTexture(*context.vk, fallback_normal_texture_);

    if (depth_shader_id_ == 0) {
        depth_shader_id_ = shader_mgr.LoadShader("compiled_shaders/shadow_depth");
        if (depth_shader_id_ == 0) {
            LOGE("DepthPrepass failed to load depth shader");
            return;
        }
    }
    uint32_t const width = context.depth_target.extent.width != 0
        ? context.depth_target.extent.width
        : context.color_target.extent.width;
    uint32_t const height = context.depth_target.extent.height != 0
        ? context.depth_target.extent.height
        : context.color_target.extent.height;
    if (depth_texture.IsInitialized()) {
        auto const extent = depth_texture.Extent();
        if (extent.width != width || extent.height != height) {
            depth_texture.Shutdown(*context.vk);
            depth_texture_ready = 0u;
        }
    }

    if (!depth_texture.IsInitialized()) {
        if (!depth_texture.Init(*context.vk, vkfw::TextureInfo{
                                                .width = width,
                                                .height = height,
                                                .mip_levels = 1,
                                                .format = vkfw::TextureFormat::D32_SFLOAT,
                                                .usage = vkfw::TextureUsage::DepthStencilAttachment,
                                                .mipmap = false,
        })) {
            LOGE("DepthPrepass failed to create depth texture");
            return;
        }
        depth_texture_ready = 0u;
    }

    if (depth_texture_ready == 0u) {
        TransitionImageLayout(context.command_buffer,
                              depth_texture.Handle(),
                              vk::ImageAspectFlagBits::eDepth,
                              vk::ImageLayout::eUndefined,
                              vk::ImageLayout::eDepthAttachmentOptimal,
                              {},
                              vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                              vk::PipelineStageFlagBits::eTopOfPipe,
                              vk::PipelineStageFlagBits::eEarlyFragmentTests);
    }

    vk::ClearDepthStencilValue clear_depth{};
    clear_depth.depth = 1.0f;
    clear_depth.stencil = 0;
    if (!BeginDepthOnlyRendering(context, depth_texture, vk::Extent2D{width, height}, clear_depth)) {
        static bool logged_error = false;
        if (!logged_error) {
            LOGE("DepthPrepass failed to begin depth-only rendering");
            logged_error = true;
        }
        return;
    }
    DepthFrameUbo frame_ubo{};
    if (auto const* frame_view = CurrentFrameView(context)) {
        frame_ubo.view_projection = GpuMatrix(frame_view->view_projection);
    }

    if (!frame_binding.ubo.IsInitialized()) {
        frame_binding.ubo.Init(*context.vk, vkfw::BufferInfo{
                                                .size = static_cast<uint32_t>(sizeof(DepthFrameUbo)),
                                                .usage = vkfw::BufferUsage::Uniform,
                                                .mappable = true,
                                            });
    }
    frame_binding.ubo.UpdateData(*context.vk, &frame_ubo, static_cast<uint32_t>(sizeof(DepthFrameUbo)));

    if (frame_binding.descriptor_set_id == 0) {
        uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
        frame_binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(frame_layout_id);
    }
    if (frame_binding.descriptor_set_id != 0) {
        desc_alloc.UpdateUniformBuffer(frame_binding.descriptor_set_id, 0, frame_binding.ubo.Handle(), 0, sizeof(DepthFrameUbo));
    }

    auto const* shader = shader_mgr.GetShader(depth_shader_id_);
    if (shader == nullptr || shader->vertex_shader == nullptr) {
        EndSwapchainRendering(context);
        return;
    }

    PipelineKey cached_key{};
    uint32_t cached_pipeline_id = 0;

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

        PipelineKey key = MakePipelineKey(shader->id, *mesh);
        key.layout_profile = PipelineLayoutProfile::Material_Set0_Set1;
        key.depth_format = static_cast<uint32_t>(vk::Format::eD32Sfloat);
        key.viewport_width = width;
        key.viewport_height = height;

        if (cached_pipeline_id == 0 || key.shader_id != cached_key.shader_id || key.vertex_layout_id != cached_key.vertex_layout_id ||
            key.depth_format != cached_key.depth_format || key.viewport_width != cached_key.viewport_width ||
            key.viewport_height != cached_key.viewport_height) {
            cached_key = key;
            cached_pipeline_id = context.pipelines->GetPipelineCache().GetOrCreatePipeline(
                key,
                context.color_target.compatibility_render_pass);
        }

        if (cached_pipeline_id == 0) {
            continue;
        }
        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(cached_pipeline_id);
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
        object_push.world = GpuMatrix(renderable->world);
        context.command_buffer.pushConstants(pipeline->Layout(),
                                             vk::ShaderStageFlagBits::eVertex,
                                             0,
                                             sizeof(ObjectPushConstants),
                                             &object_push);

        vk::DescriptorSet frame_set{};
        if (frame_binding.descriptor_set_id != 0) {
            frame_set = desc_alloc.GetHandle(frame_binding.descriptor_set_id);
        }
        if (frame_set) {
            context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                      pipeline->Layout(),
                                                      0,
                                                      1,
                                                      &frame_set,
                                                      0,
                                                      nullptr);
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

    EndSwapchainRendering(context);
    depth_texture_ready = 1u;
}

} // namespace ave::render
