#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {

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
        context.command_buffer == vk::CommandBuffer{} || context.swapchain == nullptr) {
        return;
    }

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& shader_mgr = context.resources->GetShaderManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    struct DepthFrameUbo {
        glm::mat4 view_projection{1.0f};
    };

    struct ObjectPushConstants {
        glm::mat4 world{1.0f};
    };

    if (depth_shader_id_ == 0) {
        depth_shader_id_ = shader_mgr.LoadShader("compiled_shaders/shadow_depth");
        if (depth_shader_id_ == 0) {
            LOGE("DepthPrepass failed to load depth shader");
            return;
        }
    }

    uint32_t const width = context.swapchain->Extent().width;
    uint32_t const height = context.swapchain->Extent().height;
    if (depth_texture_.IsInitialized()) {
        auto const extent = depth_texture_.Extent();
        if (extent.width != width || extent.height != height) {
            depth_texture_.Shutdown(*context.vk);
            depth_texture_ready_ = false;
        }
    }

    if (!depth_texture_.IsInitialized()) {
        if (!depth_texture_.Init(*context.vk, vkfw::TextureInfo{
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
        depth_texture_ready_ = false;
    }

    if (!depth_texture_ready_) {
        TransitionImageLayout(context.command_buffer,
                              depth_texture_.Handle(),
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
    if (!BeginDepthOnlyRendering(context, depth_texture_, context.swapchain->Extent(), clear_depth)) {
        LOGE("DepthPrepass failed to begin depth-only rendering");
        return;
    }
    context.current_depth_texture = &depth_texture_;

    DepthFrameUbo frame_ubo{};
    if (context.frame != nullptr) {
        frame_ubo.view_projection = context.frame->view.view_projection;
    }

    if (!frame_ubo_.IsInitialized()) {
        frame_ubo_.Init(*context.vk, vkfw::BufferInfo{
                                         .size = static_cast<uint32_t>(sizeof(DepthFrameUbo)),
                                         .usage = vkfw::BufferUsage::Uniform,
                                         .mappable = true,
                                     });
    }
    frame_ubo_.UpdateData(*context.vk, &frame_ubo, static_cast<uint32_t>(sizeof(DepthFrameUbo)));

    if (frame_set_id_ == 0) {
        uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
        frame_set_id_ = desc_alloc.AllocateDescriptorSet(frame_layout_id);
    }
    if (frame_set_id_ != 0) {
        desc_alloc.UpdateUniformBuffer(frame_set_id_, 0, frame_ubo_.Handle(), 0, sizeof(DepthFrameUbo));
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

        PipelineKey key = MakePipelineKey(shader->id, *mesh);
        key.layout_profile = PipelineLayoutProfile::Global_Set0_Only;
        key.depth_format = static_cast<uint32_t>(vk::Format::eD32Sfloat);
        key.viewport_width = context.swapchain->Extent().width;
        key.viewport_height = context.swapchain->Extent().height;

        if (cached_pipeline_id == 0 || key.shader_id != cached_key.shader_id || key.vertex_layout_id != cached_key.vertex_layout_id ||
            key.depth_format != cached_key.depth_format || key.viewport_width != cached_key.viewport_width ||
            key.viewport_height != cached_key.viewport_height) {
            cached_key = key;
            cached_pipeline_id = context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        }

        if (cached_pipeline_id == 0) {
            continue;
        }
        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(cached_pipeline_id);
        if (!pipeline) {
            continue;
        }

        context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

        ObjectPushConstants object_push{};
        object_push.world = renderable->world;
        context.command_buffer.pushConstants(pipeline->Layout(),
                                             vk::ShaderStageFlagBits::eVertex,
                                             0,
                                             sizeof(ObjectPushConstants),
                                             &object_push);

        vk::DescriptorSet frame_set{};
        if (frame_set_id_ != 0) {
            frame_set = desc_alloc.GetHandle(frame_set_id_);
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
    depth_texture_ready_ = true;
}

} // namespace ave::render
