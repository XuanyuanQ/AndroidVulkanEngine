#include "ave/render/FrameGraph.h"

namespace ave::render {

void FrameGraph::AddPass(std::unique_ptr<RenderPass> pass)
{
    passes_.push_back(std::move(pass));
}

void FrameGraph::Execute(RenderPassContext const& context)
{
    for (auto& pass : passes_) {
        pass->Execute(context);
    }
}

size_t FrameGraph::PassCount() const noexcept
{
    return passes_.size();
}

} // namespace ave::render
