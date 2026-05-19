#include "ave/render/FrameGraph.h"

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

void FrameGraph::Execute(RenderPassContext const& context)
{
    if (context.frame == nullptr) {
        return;
    }

    for (auto& node : passes_) {
        if (!node.pass) {
            continue;
        }

        PassDataFilter const filter = node.has_filter_override ? node.filter_override
                                                               : node.pass->GetDataFilter();
        PassExecutionView const view = BuildPassView(*context.frame, filter);
        node.pass->Execute(context, view);
    }
}

size_t FrameGraph::PassCount() const noexcept
{
    return passes_.size();
}

} // namespace ave::render
