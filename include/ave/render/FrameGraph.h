#pragma once

#include "ave/render/RenderPass.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ave::render {

class FrameGraph {
public:
    void AddPass(std::unique_ptr<RenderPass> pass);
    void AddPass(std::unique_ptr<RenderPass> pass, PassDataFilter const& filter_override);
    void Preload(RenderPassContext const& context);
    void Execute(RenderPassContext const& context);
    void ResetRuntimeState(vkfw::VkContext* ctx = nullptr);
    void Clear(vkfw::VkContext* ctx = nullptr);
    bool HasPass(std::string_view name) const noexcept;
    std::string DescribePasses() const;
    size_t PassCount() const noexcept;

private:
    struct PassNode {
        std::unique_ptr<RenderPass> pass{};
        PassDataFilter filter_override{};
        bool has_filter_override = false;
    };

    std::vector<PassNode> passes_;
    mutable FrameGraphResourceRegistry resources_{};
};

} // namespace ave::render
