#pragma once

#include "ave/render/RenderPass.h"

#include <memory>
#include <vector>

namespace ave::render {

class FrameGraph {
public:
    void AddPass(std::unique_ptr<RenderPass> pass);
    void AddPass(std::unique_ptr<RenderPass> pass, PassDataFilter const& filter_override);
    void Execute(RenderPassContext const& context);
    size_t PassCount() const noexcept;

private:
    struct PassNode {
        std::unique_ptr<RenderPass> pass{};
        PassDataFilter filter_override{};
        bool has_filter_override = false;
    };

    std::vector<PassNode> passes_;
};

} // namespace ave::render
