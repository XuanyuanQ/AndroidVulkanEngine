#include "ave/render/RenderPasses.h"

#include "ave/render/PipelineSystem.h"
#include "ave/resource/ResourceSystem.h"
#include "VkDescriptor.hpp"
#include "VkPipeline.hpp"
#include "ave/project/SharedDataContract.h"

#include "VkPipeline.hpp"
#include "VkSwapchain.hpp"
#include <android/log.h>

#include <cstddef>
#include <string>
#include <vector>

namespace ave::render {
namespace {

void Emit(RenderPassContext const& ctx, std::string const& line)
{
    if (ctx.debug_output != nullptr) {
        ctx.debug_output->push_back(line);
    }
}

uint32_t VertexLayoutIdFromMesh(ave::resource::MeshRuntime const& mesh)
{
    // Convention (see README): vertex_layout_id describes attribute layout.
    // For bring-up we key by stride; extend this when multiple layouts share stride.
    if (mesh.vertex_stride == 7 * sizeof(float)) {
        return 1; // RasterColorVertex (pos3 + color4)
    }
    if (mesh.vertex_stride == sizeof(ave::project::VertexData)) {
        return 2; // project::VertexData
    }
    return 0;
}

DescriptorSetLayoutKey MakeFrameSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eUniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eAllGraphics),
        },
        DescriptorBinding{
            .binding = 1,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eCombinedImageSampler),
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
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eUniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 1,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eCombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 2,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eCombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 3,
            .descriptor_type = static_cast<uint32_t>(vk::DescriptorType::eCombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
    };
    return key;
}

PipelineKey MakePipelineKey(RenderPassContext const& ctx,
                            uint32_t pass_id,
                            uint32_t shader_id,
                            ave::resource::MeshRuntime const& mesh)
{
    PipelineKey key{};
    key.pass_id = pass_id;
    key.shader_id = shader_id;
    key.vertex_layout_id = VertexLayoutIdFromMesh(mesh);
    key.render_state_id = 1;
    key.layout_profile = 0; // Preview FrameData path: no descriptor sets yet.
    key.rt_format = 0;      // filled by caller when swapchain is present
    key.depth_format = 0;
    key.stencil_format = 0;
    key.sample_count = 1;
    key.viewport_width = 0;
    key.viewport_height = 0;

    (void)ctx;
    return key;
}

} // namespace

PassDataFilter DepthPrepass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::DepthPrepass;
    filter.opaque_only = true;
    return filter;
}

void DepthPrepass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: DepthPrepass");
    (void)view;
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
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ShadowPass");

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    // Vulkan backend detection
    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();

    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    // For shadow pass we only need material (if any) and mesh data.
    // No frame UBO is required.

    for (auto const* renderable : view.renderables) {
        if (!renderable) continue;

        // Resolve material
        auto const* material = mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing material %s", renderable->material_id.c_str());
            continue;
        }

        // Resolve mesh
        auto const* mesh = mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing mesh %s", renderable->mesh_id.c_str());
            continue;
        }

        // Resolve shader (fallback to material's shader)
        ave::resource::ShaderRuntime const* shader = nullptr;
        if (material != nullptr && material->shader_id != 0) {
            shader = shader_mgr.GetShader(material->shader_id);
        }
        if (!shader) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing shader for material %s", material->name.c_str());
            continue;
        }

        // Create pipeline key. Use pass_id = 1 for shadow (arbitrary distinct value).
        PipelineKey key = MakePipelineKey(context, /*pass_id*/ 1, shader->id, *mesh);
        // Shadow pass does not require any descriptor sets, keep layout_profile = 0.
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }
        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: pipeline create failed for %s", renderable->debug_name.c_str());
            continue;
        }

        if (has_vk) {
            auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
            if (!pipeline) continue;

            // Bind pipeline
            context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

            // Bind vertex and index buffers
            vk::DeviceSize offset = 0;
            context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
            if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
                context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
                uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
                uint32_t const first_index = renderable->first_index;
                int32_t const vertex_offset = static_cast<int32_t>(renderable->first_vertex);
                context.command_buffer.drawIndexed(index_count, 1, first_index, vertex_offset, 0);
            } else {
                uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
                uint32_t const first_vertex = renderable->first_vertex;
                context.command_buffer.draw(vertex_count, 1, first_vertex, 0);
            }
        }

        __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  draw: %s", renderable->debug_name.c_str());
    }
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
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: PBRPass");

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    // If we're running on the Vulkan backend, record real draw calls.
    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();

    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    struct FrameUbo {
        glm::mat4 view_projection;
    };

    FrameUbo frame_ubo{};
    if (context.frame != nullptr) {
        frame_ubo.view_projection = context.frame->view.view_projection;
    }

    if (has_vk) {
        if (!frame_ubo_.IsInitialized()) {
            frame_ubo_.Init(*context.vk, vkfw::BufferInfo{
                                            .size = static_cast<uint32_t>(sizeof(FrameUbo)),
                                            .usage = vkfw::BufferUsage::Uniform,
                                            .mappable = true,
                                        });
        }
        frame_ubo_.UpdateData(*context.vk, &frame_ubo, static_cast<uint32_t>(sizeof(FrameUbo)));

        if (frame_set_id_ == 0) {
            uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
            frame_set_id_ = desc_alloc.AllocateDescriptorSet(frame_layout_id);
        }
        if (frame_set_id_ != 0) {
            desc_alloc.UpdateUniformBuffer(frame_set_id_, /*binding*/ 0, frame_ubo_.Handle(), 0, sizeof(FrameUbo));
        }
    }
    for (auto const* renderable : view.renderables) {
        if (!renderable) {
            continue;
        }
        __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "frame_index: %llu", context.frame->frame_index);
        auto const* material = mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing material %s, using default", renderable->material_id.c_str());
            continue;
        }   
        

        auto const* mesh = mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing mesh %s", renderable->mesh_id.c_str());
            continue;
        }
        ave::resource::ShaderRuntime const* shader = nullptr;
        // Fallback to the material's loaded shader if not explicitly specified on the renderable
        if (!shader && material != nullptr && material->shader_id != 0) {
            shader = shader_mgr.GetShader(material->shader_id);
        }

        if (!shader) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: missing shader");
            continue;
        }

        PipelineKey key = MakePipelineKey(context, /*pass_id*/ 0, shader->id, *mesh);
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }
        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip: pipeline create failed for %s", renderable->debug_name.c_str());
            continue;
        }

        if (has_vk) {
            auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
            if (!pipeline) {
                continue;
            }

            // Bind pipeline + descriptors + vertex buffer.
            context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

            // if (frame_set_id_ != 0) {
            //     vk::DescriptorSet desc_set = desc_alloc.GetHandle(frame_set_id_);
            //     if (desc_set) {
            //         context.command_buffer.bindDescriptorSets(
            //             pipeline->BindPoint(),
            //             pipeline->Layout(),
            //             0, 1, &desc_set, 0, nullptr);
            //     }
            // }

            vk::DeviceSize offset = 0;
            context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
            if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
                context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
                uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
                uint32_t const first_index = renderable->first_index;
                int32_t const vertex_offset = static_cast<int32_t>(renderable->first_vertex);
                context.command_buffer.drawIndexed(index_count, 1, first_index, vertex_offset, 0);
            } else {
                uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
                uint32_t const first_vertex = renderable->first_vertex;
                context.command_buffer.draw(vertex_count, 1, first_vertex, 0);
            }
        }

        __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  draw: %s", renderable->debug_name.c_str());
    }
}

PassDataFilter ComputePass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::Compute;
    return filter;
}

void ComputePass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ComputePass");
    (void)view;
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
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: UIPass");
    for (auto const* item : view.ui_items) {
        if (!item) {
            continue;
        }
        __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  ui: %s", item->debug_name.c_str());
    }
}

PassDataFilter ToneMappingPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ToneMapping;
    return filter;
}

void ToneMappingPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ToneMappingPass");
    (void)view;
}

} // namespace ave::render
