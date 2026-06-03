#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {


void UIPass::Reset(vkfw::VkContext* ctx)
{
    texture_descriptor_sets_.clear();
    ui_shader_id_ = 0;
    if (ctx != nullptr) {
        if (ui_vertex_buffers_[0].IsInitialized()) {
            ui_vertex_buffers_[0].Shutdown(*ctx);
        }
        if (ui_vertex_buffers_[1].IsInitialized()) {
            ui_vertex_buffers_[1].Shutdown(*ctx);
        }
        if (ui_index_buffers_[0].IsInitialized()) {
            ui_index_buffers_[0].Shutdown(*ctx);
        }
        if (ui_index_buffers_[1].IsInitialized()) {
            ui_index_buffers_[1].Shutdown(*ctx);
        }
        if (fallback_white_texture_.IsInitialized()) {
            fallback_white_texture_.Shutdown(*ctx);
        }
    }
}

PassDataFilter UIPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::UI;
    filter.layer_mask = core::ToMask(core::RenderLayer::UI);
    return filter;
}

void UIPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    using namespace detail;

    if (view.ui_items.empty()) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr &&
        context.swapchain != nullptr &&
        context.command_buffer != vk::CommandBuffer{} &&
        context.pipelines != nullptr &&
        context.resources != nullptr;

    uint64_t const frame_index = context.frame ? context.frame->frame_index : 0;
    uint32_t const buf_idx = static_cast<uint32_t>(frame_index % 2);

    auto& texture_mgr = context.resources->GetTextureManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();
    std::vector<ave::render::UiVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(view.ui_items.size() * 4);
    indices.reserve(view.ui_items.size() * 6);

    float const width = has_vk ? static_cast<float>(context.swapchain->Extent().width) : 1080.0f;
    float const height = has_vk ? static_cast<float>(context.swapchain->Extent().height) : 1920.0f;
    float const aspect_ratio = (width > 0.0f) ? (height / width) : (9.0f / 16.0f);

    std::vector<std::string> unique_texture_paths;
    if (has_vk) {
        for (auto const* item : view.ui_items) {
            if (!item || !item->visible || item->texture_id.empty()) {
                continue;
            }
            if (std::find(unique_texture_paths.begin(), unique_texture_paths.end(), item->texture_id) == unique_texture_paths.end()) {
                if (unique_texture_paths.size() < 15) {
                    unique_texture_paths.push_back(item->texture_id);
                }
            }
        }
    }

    std::vector<uint32_t> texture_runtime_ids(16, 0);
    for (size_t i = 0; i < unique_texture_paths.size(); ++i) {
        std::string const& path = unique_texture_paths[i];
        uint32_t runtime_id = 0;
        if (auto const* runtime = texture_mgr.GetTextureByPath(path)) {
            runtime_id = runtime->id;
        } else {
            runtime_id = texture_mgr.LoadTexture(path);
        }
        texture_runtime_ids[i + 1] = runtime_id;
    }

    for (auto const* item : view.ui_items) {
        if (!item || !item->visible) {
            continue;
        }

        uint32_t texture_index = 0;
        if (!item->texture_id.empty()) {
            auto it = std::find(unique_texture_paths.begin(), unique_texture_paths.end(), item->texture_id);
            if (it != unique_texture_paths.end()) {
                texture_index = static_cast<uint32_t>(1 + std::distance(unique_texture_paths.begin(), it));
            }
        }

        AppendUiQuad(vertices, indices, *item, texture_index, aspect_ratio);
    }

    if (!has_vk || vertices.empty() || indices.empty()) {
        return;
    }

    auto& shader_mgr = context.resources->GetShaderManager();
    if (ui_shader_id_ == 0) {
        ui_shader_id_ = shader_mgr.LoadShader("compiled_shaders/ui_textured");
        if (ui_shader_id_ == 0) {
            LOGE("UIPass failed to load ui_textured shader");
            return;
        }
    }

    uint32_t const vertex_bytes = static_cast<uint32_t>(vertices.size() * sizeof(ave::render::UiVertex));
    uint32_t const index_bytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));

    if (!ui_vertex_buffers_[buf_idx].IsInitialized() || ui_vertex_buffers_[buf_idx].Size() < vertex_bytes) {
        if (ui_vertex_buffers_[buf_idx].IsInitialized()) {
            ui_vertex_buffers_[buf_idx].Shutdown(*context.vk);
        }
        if (!ui_vertex_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                                                     .size = vertex_bytes,
                                                     .usage = vkfw::BufferUsage::Vertex,
                                                     .mappable = true,
                                                 })) {
            LOGE("UIPass failed to create vertex buffer");
            return;
        }
    }
    if (!ui_index_buffers_[buf_idx].IsInitialized() || ui_index_buffers_[buf_idx].Size() < index_bytes) {
        if (ui_index_buffers_[buf_idx].IsInitialized()) {
            ui_index_buffers_[buf_idx].Shutdown(*context.vk);
        }
        if (!ui_index_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                                                    .size = index_bytes,
                                                    .usage = vkfw::BufferUsage::Index,
                                                    .mappable = true,
                                                })) {
            LOGE("UIPass failed to create index buffer");
            return;
        }
    }

    ui_vertex_buffers_[buf_idx].UpdateData(*context.vk, vertices.data(), vertex_bytes);
    ui_index_buffers_[buf_idx].UpdateData(*context.vk, indices.data(), index_bytes);

    vk::ClearValue clear{};
    clear.color.float32[0] = 0.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f;
    clear.color.float32[3] = 0.0f;
    if (!BeginSwapchainRendering(context, clear, false)) {
        LOGE("UIPass failed to begin rendering");
        return;
    }

    ave::resource::MeshRuntime ui_mesh{};
    ui_mesh.vertex_stride = sizeof(ave::render::UiVertex);

    PipelineKey key = MakePipelineKey(ui_shader_id_, ui_mesh);
    key.vertex_layout_id = 2;
    key.layout_profile = PipelineLayoutProfile::Texture_Set0_Only;
    key.render_state_id = 2;
    key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
    key.viewport_width = context.swapchain->Extent().width;
    key.viewport_height = context.swapchain->Extent().height;

    vk::RenderPass const ui_compatibility_render_pass =
        context.compatibility_load_render_pass != vk::RenderPass{}
            ? context.compatibility_load_render_pass
            : context.compatibility_render_pass;
    uint32_t const pipeline_id =
        context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, ui_compatibility_render_pass);
    auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
    if (!pipeline) {
        EndSwapchainRendering(context);
        LOGE("UIPass failed to create pipeline");
        return;
    }

    context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

    vk::DeviceSize offset = 0;
    context.command_buffer.bindVertexBuffers(0, ui_vertex_buffers_[buf_idx].Handle(), offset);
    context.command_buffer.bindIndexBuffer(ui_index_buffers_[buf_idx].Handle(), 0, vk::IndexType::eUint32);

    uint32_t const texture_layout_id = desc_cache.GetOrCreateLayout(MakeTextureSetLayoutKey());
    vk::Sampler const sampler = GetCommonSampler(*context.vk);
    EnsureFallbackWhiteTexture(*context.vk, fallback_white_texture_);

    uint32_t descriptor_set_id = texture_descriptor_sets_[buf_idx];
    if (descriptor_set_id == 0) {
        descriptor_set_id = desc_alloc.AllocateDescriptorSet(texture_layout_id);
        texture_descriptor_sets_[buf_idx] = descriptor_set_id;
    }

    if (descriptor_set_id != 0) {
        desc_alloc.UpdateImageSamplerArray(descriptor_set_id,
                                           0,
                                           0,
                                           sampler,
                                           fallback_white_texture_.View(),
                                           vk::ImageLayout::eShaderReadOnlyOptimal);
        for (uint32_t slot = 1; slot < 16; ++slot) {
            uint32_t const runtime_id = texture_runtime_ids[slot];
            vkfw::VkTexture const* texture = ResolveTextureOrFallback(*context.vk, texture_mgr, runtime_id, fallback_white_texture_);
            if (texture != nullptr) {
                desc_alloc.UpdateImageSamplerArray(descriptor_set_id,
                                                   0,
                                                   slot,
                                                   sampler,
                                                   texture->View(),
                                                   vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }

        vk::DescriptorSet const set = desc_alloc.GetHandle(descriptor_set_id);
        if (set) {
            context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                      pipeline->Layout(),
                                                      0,
                                                      1,
                                                      &set,
                                                      0,
                                                      nullptr);
        }
    }

    context.command_buffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    EndSwapchainRendering(context);
}

} // namespace ave::render
