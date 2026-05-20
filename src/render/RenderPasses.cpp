#include "ave/render/RenderPasses.h"

#include "ave/render/PipelineSystem.h"
#include "ave/resource/ResourceSystem.h"
#include "VkDescriptor.hpp"
#include "VkPipeline.hpp"

#include "VkSwapchain.hpp"
#include <android/log.h>

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
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::UniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eAllGraphics),
        },
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
    key.layout_profile = 2; // Set0 frame + Set1 material
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
    Emit(context, "Pass: DepthPrepass");
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
    Emit(context, "Pass: ShadowPass");
    (void)view;
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
    Emit(context, "Pass: PBRPass");

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
        float view_projection[16]{};
    };

    FrameUbo frame_ubo{};
    if (context.frame != nullptr) {
        for (int i = 0; i < 16; ++i) {
            frame_ubo.view_projection[i] = context.frame->view.view_projection[i];
        }
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
        static ave::resource::MaterialRuntime default_material{
            .id = 999999,
            .name = "default_fallback",
            .shader_id = 0,
            .base_color_texture = 0,
            .normal_texture = 0,
            .metallic_roughness_texture = 0,
            .base_color = {1.0f, 1.0f, 1.0f, 1.0f},
            .metallic = 0.0f,
            .roughness = 0.5f,
            .is_loaded = true
        };
        auto const* material = mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            Emit(context, "  skip: missing material '" + renderable->material_id + "', using default");
            material = &default_material;
        }   
        

        auto const* mesh = mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            Emit(context, "  skip: missing mesh '" + renderable->mesh_id + "'");
            continue;
        }
        auto const* shader = shader_mgr.GetShaderByPath(renderable->shader_id); //for bring-up,loading shader no via material
        if (!shader) {
            Emit(context, "  skip: missing shader '" + renderable->shader_id + "'");
            continue;
        }

        PipelineKey key = MakePipelineKey(context, /*pass_id*/ 0, shader->id, *mesh);
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }
        uint32_t const pipeline_id = context.pipelines->GetPipelineCache().GetOrCreatePipeline(key);
        if (pipeline_id == 0) {
            Emit(context, "  skip: pipeline create failed for '" + renderable->debug_name + "'");
            continue;
        }

        if (has_vk) {
            auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
            if (!pipeline) {
                continue;
            }

            // Material descriptor set (UBO only for now).
            auto& binding = material_bindings_[material->id];
            if (!binding.ubo.IsInitialized()) {
                struct MaterialUbo {
                    float base_color[4]{};
                    float metallic = 0.0f;
                    float roughness = 0.0f;
                    float _pad[2]{};
                };
                binding.ubo.Init(*context.vk, vkfw::BufferInfo{
                                                .size = static_cast<uint32_t>(sizeof(MaterialUbo)),
                                                .usage = vkfw::BufferUsage::Uniform,
                                                .mappable = true,
                                            });
            }

            struct MaterialUbo {
                float base_color[4]{};
                float metallic = 0.0f;
                float roughness = 0.0f;
                float _pad[2]{};
            } mat_ubo{};
            mat_ubo.base_color[0] = material->base_color[0];
            mat_ubo.base_color[1] = material->base_color[1];
            mat_ubo.base_color[2] = material->base_color[2];
            mat_ubo.base_color[3] = material->base_color[3];
            mat_ubo.metallic = material->metallic;
            mat_ubo.roughness = material->roughness;
            binding.ubo.UpdateData(*context.vk, &mat_ubo, static_cast<uint32_t>(sizeof(MaterialUbo)));

            if (binding.descriptor_set_id == 0) {
                uint32_t const material_layout_id = desc_cache.GetOrCreateLayout(MakeMaterialSetLayoutKey());
                binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(material_layout_id);
            }
            if (binding.descriptor_set_id != 0) {
                desc_alloc.UpdateUniformBuffer(binding.descriptor_set_id, /*binding*/ 0, binding.ubo.Handle(), 0, sizeof(MaterialUbo));
            }

            // Bind pipeline + descriptors + vertex buffer.
            context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

            std::vector<vk::DescriptorSet> sets;
            sets.reserve(2);
            if (frame_set_id_ != 0) {
                sets.push_back(desc_alloc.GetHandle(frame_set_id_));
            }
            if (binding.descriptor_set_id != 0) {
                sets.push_back(desc_alloc.GetHandle(binding.descriptor_set_id));
            }
            if (!sets.empty()) {
                context.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                         pipeline->Layout(),
                                                         /*firstSet*/ 0,
                                                         static_cast<uint32_t>(sets.size()),
                                                         sets.data(),
                                                         0,
                                                         nullptr);
            }

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

        Emit(context, "  draw: " + renderable->debug_name);
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
    Emit(context, "Pass: ComputePass");
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
    Emit(context, "Pass: UIPass");
    for (auto const* item : view.ui_items) {
        if (!item) {
            continue;
        }
        Emit(context, "  ui: " + item->debug_name);
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
    Emit(context, "Pass: ToneMappingPass");
    (void)view;
}

} // namespace ave::render
