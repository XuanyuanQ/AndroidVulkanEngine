#include "ave/render/AndroidSurfaceRenderBackend.h"

#include "VkContext.hpp"
#include "LogUtil.h"

namespace ave::render {

AndroidSurfaceRenderBackend::AndroidSurfaceRenderBackend(AndroidSurfaceRenderResources& resources,
                                                         core::FrameData const& frame,
                                                         vkfw::VkContext& ctx,
                                                         vkfw::VkSwapchain& swapchain,
                                                         vkfw::VkFrameSync& sync,
                                                         uint32_t& frame_index)
    : resources_(resources)
    , frame_(frame)
    , ctx_(ctx)
    , swapchain_(swapchain)
    , sync_(sync)
    , frame_index_(frame_index)
{
}

FrameGraphRenderResult AndroidSurfaceRenderBackend::BeginFrame(RenderFrameRequest& out_request)
{
    sync_.WaitForFrame(ctx_, frame_index_);
    auto [acq_result, image_index] =
        swapchain_.AcquireNextImage(UINT64_MAX, sync_.ImageAvailable(frame_index_), vk::Fence{});
    acquire_result_ = acq_result;
    if (acq_result == vk::Result::eErrorOutOfDateKHR) {
        LOGW("AndroidSurfaceRenderBackend acquire out-of-date");
        return FrameGraphRenderResult::SwapchainOutOfDate;
    }
    if (acq_result != vk::Result::eSuccess && acq_result != vk::Result::eSuboptimalKHR) {
        LOGW("AndroidSurfaceRenderBackend acquire skipped result=%d", static_cast<int>(acq_result));
        return FrameGraphRenderResult::Skipped;
    }

    image_index_ = image_index;
    if (resources_.image_in_flight_fences.size() != swapchain_.ImageCount()) {
        resources_.image_in_flight_fences.assign(swapchain_.ImageCount(), vk::Fence{});
    }
    vk::Fence const image_fence = resources_.image_in_flight_fences[image_index_];
    if (image_fence != vk::Fence{}) {
        auto const wait_result = ctx_.Device().waitForFences(image_fence, vk::True, UINT64_MAX);
        if (wait_result != vk::Result::eSuccess) {
            LOGW("AndroidSurfaceRenderBackend image fence wait failed result=%d", static_cast<int>(wait_result));
            return FrameGraphRenderResult::Skipped;
        }
    }
    resources_.image_in_flight_fences[image_index_] = sync_.InFlightFence(frame_index_);

    sync_.ResetFence(ctx_, frame_index_);
    resources_.framegraph_command_buffers.Reset(frame_index_);
    cmd_ = resources_.framegraph_command_buffers.Handle(frame_index_);
    cmd_.begin(vk::CommandBufferBeginInfo{});

    swap_img_ = swapchain_.Image(image_index_);
    vk::ImageLayout const old_layout = swapchain_.IsFirstUse(image_index_)
        ? vk::ImageLayout::eUndefined
        : vk::ImageLayout::ePresentSrcKHR;

    vk::ImageMemoryBarrier to_color{};
    to_color.oldLayout = old_layout;
    to_color.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.image = swap_img_;
    to_color.subresourceRange = ColorRange();
    to_color.srcAccessMask = {};
    to_color.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    cmd_.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                         vk::PipelineStageFlagBits::eColorAttachmentOutput,
                         {}, {}, {}, to_color);

    RenderViewTarget target{};
    target.color_target.image = swapchain_.Image(image_index_);
    target.color_target.image_view = swapchain_.ImageView(image_index_);
    target.color_target.format = swapchain_.Format();
    target.color_target.extent = swapchain_.Extent();
    target.view_index = 0;
    target.frame_resource_index = image_index_;
    target.frame_resource_count = swapchain_.ImageCount();
    if (image_index_ < resources_.depth_textures.size()) {
        target.depth_target.texture = &resources_.depth_textures[image_index_];
        target.depth_target.extent = swapchain_.Extent();
        target.depth_target.format = vk::Format::eD32Sfloat;
    }
    if (image_index_ < resources_.depth_texture_ready.size()) {
        target.depth_target.ready = &resources_.depth_texture_ready[image_index_];
    }
    if (!ctx_.SupportsDynamicRendering()) {
        target.color_target.compatibility_render_pass = resources_.framegraph_render_pass.Handle();
        target.color_target.compatibility_framebuffer = resources_.framegraph_framebuffers.Handle(image_index_);
        target.color_target.compatibility_load_render_pass = resources_.framegraph_load_render_pass.Handle();
        target.color_target.compatibility_load_framebuffer = resources_.framegraph_load_framebuffers.Handle(image_index_);
    }

    out_request = {};
    out_request.frame = &frame_;
    out_request.vk = &ctx_;
    out_request.command_buffer = cmd_;
    out_request.views.push_back(target);
    frame_started_ = true;
    return FrameGraphRenderResult::Success;
}

FrameGraphRenderResult AndroidSurfaceRenderBackend::EndFrame(FrameGraphRenderResult render_result)
{
    if (!frame_started_) {
        return render_result;
    }

    vk::ImageMemoryBarrier to_present{};
    to_present.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image = swap_img_;
    to_present.subresourceRange = ColorRange();
    to_present.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    to_present.dstAccessMask = {};
    cmd_.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                         vk::PipelineStageFlagBits::eBottomOfPipe,
                         {}, {}, {}, to_present);

    cmd_.end();
    swapchain_.MarkUsed(image_index_);

    vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submit{};
    submit.waitSemaphoreCount = 1;
    auto image_avail = sync_.ImageAvailable(frame_index_);
    submit.pWaitSemaphores = &image_avail;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_;
    submit.signalSemaphoreCount = 1;
    auto render_finished = sync_.RenderFinished(image_index_);
    submit.pSignalSemaphores = &render_finished;
    ctx_.GraphicsQueue().submit(submit, sync_.InFlightFence(frame_index_));

    vk::PresentInfoKHR present{};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished;
    present.swapchainCount = 1;
    auto handle = swapchain_.Handle();
    present.pSwapchains = &handle;
    present.pImageIndices = &image_index_;
    vk::Result const present_result = ctx_.GraphicsQueue().presentKHR(present);

    frame_index_ = (frame_index_ + 1) % sync_.FramesInFlight();
    if (acquire_result_ == vk::Result::eSuboptimalKHR ||
        present_result == vk::Result::eSuboptimalKHR ||
        present_result == vk::Result::eErrorOutOfDateKHR) {
        return FrameGraphRenderResult::SwapchainOutOfDate;
    }
    return render_result;
}

vk::ImageSubresourceRange AndroidSurfaceRenderBackend::ColorRange()
{
    return vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
}

} // namespace ave::render
