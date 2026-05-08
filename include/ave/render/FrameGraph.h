#pragma once

#include "ave/render/RenderPass.h"

#include <memory>
#include <vector>

namespace ave::render {

class FrameGraph {
public:
    void AddPass(std::unique_ptr<RenderPass> pass);
    void Execute(RenderPassContext const& context);
    size_t PassCount() const noexcept;

private:
    std::vector<std::unique_ptr<RenderPass>> passes_;
};

} // namespace ave::render
