#include "ave/render/FrameGraph.h"
#include "VkContext.hpp"
#include "VkSwapchain.hpp"

namespace ave::render {

void FrameGraph::AddPass(std::unique_ptr<RenderPass> pass)
{
    PassNode node{};
    node.pass = std::move(pass);
    node.has_filter_override = false;
    passes_.push_back(std::move(node));
}

void FrameGraph::AddPass(std::unique_ptr<RenderPass> pass, PassDataFilter const& filter_override)
{
    PassNode node{};
    node.pass = std::move(pass);
    node.filter_override = filter_override;
    node.has_filter_override = true;
    passes_.push_back(std::move(node));
}

void FrameGraph::Preload(RenderPassContext const& context)
{
    if (context.frame == nullptr) {
        return;
    }

    for (auto& node : passes_) {
        if (!node.pass) {
            continue;
        }
        node.pass->Preload(context);
    }
}

void FrameGraph::Execute(RenderPassContext const& context)
{
    if (context.frame == nullptr) {
        return;
    }

    resources_.Clear();
    RenderPassContext frame_context = context;
    frame_context.frame_graph_resources = &resources_;

    for (size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
        auto& node = passes_[pass_index];
        if (!node.pass) {
            continue;
        }

        PassDataFilter const filter = node.has_filter_override ? node.filter_override
                                                               : node.pass->GetDataFilter();
        PassExecutionView const view = BuildPassView(*frame_context.frame, filter);
        node.pass->Execute(frame_context, view);
    }
}

void FrameGraph::ResetRuntimeState(vkfw::VkContext* ctx)
{
    for (auto& node : passes_) {
        if (node.pass) {
            node.pass->Reset(ctx);
        }
    }
}

void FrameGraph::Clear(vkfw::VkContext* ctx)
{
    ResetRuntimeState(ctx);
    passes_.clear();
    resources_.Clear();
}

bool FrameGraph::HasPass(std::string_view name) const noexcept
{
    for (auto const& node : passes_) {
        if (node.pass && node.pass->Name() == name) {
            return true;
        }
    }
    return false;
}

std::string FrameGraph::DescribePasses() const
{
    std::string result;
    for (size_t i = 0; i < passes_.size(); ++i) {
        if (i != 0) {
            result += " -> ";
        }
        result += passes_[i].pass ? std::string{passes_[i].pass->Name()} : std::string{"<null>"};
    }
    return result;
}

size_t FrameGraph::PassCount() const noexcept
{
    return passes_.size();
}

} // namespace ave::render
