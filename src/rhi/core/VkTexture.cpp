#include "VkTexture.hpp"
#include "VkContext.hpp"
#include "VkBuffer.hpp"
#include <cstring>
#include <algorithm>

namespace vkfw {

static vk::Format GetVkFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8G8B8A8_SRGB:
            return vk::Format::eR8G8B8A8Srgb;
        case TextureFormat::R8G8B8A8_UNORM:
            return vk::Format::eR8G8B8A8Unorm;
        case TextureFormat::B8G8R8A8_SRGB:
            return vk::Format::eB8G8R8A8Srgb;
        case TextureFormat::B8G8R8A8_UNORM:
            return vk::Format::eB8G8R8A8Unorm;
        case TextureFormat::R32G32B32A32_SFLOAT:
            return vk::Format::eR32G32B32A32Sfloat;
        case TextureFormat::D24_UNORM_S8_UINT:
            return vk::Format::eD24UnormS8Uint;
        case TextureFormat::D32_SFLOAT:
            return vk::Format::eD32Sfloat;
        default:
            return vk::Format::eR8G8B8A8Srgb;
    }
}

static vk::ImageUsageFlags GetImageUsageFlags(TextureUsage usage) {
    vk::ImageUsageFlags flags = vk::ImageUsageFlagBits::eTransferDst;
    
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsage::Sampled)) {
        flags |= vk::ImageUsageFlagBits::eSampled;
    }
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsage::ColorAttachment)) {
        flags |= vk::ImageUsageFlagBits::eColorAttachment;
    }
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsage::DepthStencilAttachment)) {
        flags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    }
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(TextureUsage::TransferSrc)) {
        flags |= vk::ImageUsageFlagBits::eTransferSrc;
    }
    
    return flags;
}

static vk::ImageType GetImageType(TextureInfo const& info) {
    return (info.depth == 1) ? vk::ImageType::e2D : vk::ImageType::e3D;
}

static vk::ImageViewType GetImageViewType(TextureInfo const& info) {
    if (info.cube_map) {
        return info.array_layers >= 6 ? vk::ImageViewType::eCube : vk::ImageViewType::e2DArray;
    }
    if (info.depth > 1) {
        return vk::ImageViewType::e3D;
    }
    if (info.array_layers > 1) {
        return vk::ImageViewType::e2DArray;
    }
    return vk::ImageViewType::e2D;
}

static uint32_t FindMemoryType(VkContext& ctx, uint32_t type_filter, vk::MemoryPropertyFlags properties) {
    auto memory_properties = ctx.PhysicalDevice().getMemoryProperties();
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && 
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool VkTexture::Init(VkContext& ctx, TextureInfo const& info) {
    format_ = GetVkFormat(info.format);
    usage_ = info.usage;
    extent_ = vk::Extent3D{info.width, info.height, info.depth};

    // Create image
    vk::ImageCreateInfo image_info{};
    image_info.imageType = GetImageType(info);
    image_info.extent = extent_;
    image_info.mipLevels = std::max(1u, info.mip_levels);
    image_info.arrayLayers = info.cube_map ? 6 : std::max(1u, info.array_layers);
    image_info.format = format_;
    image_info.tiling = vk::ImageTiling::eOptimal;
    image_info.initialLayout = vk::ImageLayout::eUndefined;
    image_info.usage = GetImageUsageFlags(info.usage);
    image_info.sharingMode = vk::SharingMode::eExclusive;
    if (info.cube_map) {
        image_info.flags |= vk::ImageCreateFlagBits::eCubeCompatible;
    }

    image_ = std::make_unique<vk::raii::Image>(ctx.Device(), image_info);
    image_handle_ = *image_;

    // Allocate memory
    auto memory_requirements = image_->getMemoryRequirements();
    uint32_t memory_type = FindMemoryType(ctx, memory_requirements.memoryTypeBits, 
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = memory_type;

    memory_ = std::make_unique<vk::raii::DeviceMemory>(ctx.Device(), alloc_info);
    memory_handle_ = *memory_;
    image_->bindMemory(*memory_, 0);

    // Create image view
    vk::ImageViewCreateInfo view_info{};
    view_info.image = *image_;
    view_info.viewType = GetImageViewType(info);
    view_info.format = format_;
    view_info.components.r = vk::ComponentSwizzle::eIdentity;
    view_info.components.g = vk::ComponentSwizzle::eIdentity;
    view_info.components.b = vk::ComponentSwizzle::eIdentity;
    view_info.components.a = vk::ComponentSwizzle::eIdentity;
    view_info.subresourceRange.aspectMask = (static_cast<uint32_t>(usage_) & static_cast<uint32_t>(TextureUsage::DepthStencilAttachment)) ? 
        vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = image_info.mipLevels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = info.depth > 1 ? 1u : image_info.arrayLayers;

    image_view_ = std::make_unique<vk::raii::ImageView>(ctx.Device(), view_info);
    image_view_handle_ = *image_view_;

    return true;
}

void VkTexture::Shutdown(VkContext& ctx) {
    image_view_.reset();
    image_.reset();
    memory_.reset();
    image_view_handle_ = nullptr;
    image_handle_ = nullptr;
    memory_handle_ = nullptr;
    extent_ = {};
    format_ = vk::Format::eUndefined;
}

void VkTexture::UpdateData(VkContext& ctx, void const* data, uint32_t size, uint32_t mip_level, uint32_t array_layer) {
    // Create staging buffer
    VkBuffer staging_buffer;
    BufferInfo staging_info{};
    staging_info.size = size;
    staging_info.usage = BufferUsage::Staging;
    
    if (!staging_buffer.Init(ctx, staging_info)) {
        return;
    }

    // Copy data to staging buffer
    staging_buffer.UpdateData(ctx, data, size);

    // Create temporary command buffer for transfer
    vk::CommandPoolCreateInfo pool_info{};
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = ctx.GraphicsQueueFamilyIndex();

    auto command_pool = std::make_unique<vk::raii::CommandPool>(ctx.Device(), pool_info);

    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.commandPool = *command_pool;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = 1;

    auto command_buffers = std::make_unique<vk::raii::CommandBuffers>(ctx.Device(), alloc_info);
    auto& command_buffer = command_buffers->at(0);

    // Begin command buffer
    vk::CommandBufferBeginInfo begin_info{};
    begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    command_buffer.begin(begin_info);
    
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *image_;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = mip_level;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = array_layer;
    barrier.subresourceRange.layerCount = 1;

    command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, 
        {}, {}, {}, {barrier});

    vk::BufferImageCopy copy_region{};
    copy_region.bufferOffset = 0;
    copy_region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    copy_region.imageSubresource.mipLevel = mip_level;
    copy_region.imageSubresource.baseArrayLayer = array_layer;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset = {0, 0, 0};
    copy_region.imageExtent = {
        std::max(1u, extent_.width >> mip_level),
        std::max(1u, extent_.height >> mip_level),
        extent_.depth,
    };

    command_buffer.copyBufferToImage(staging_buffer.Handle(), *image_, vk::ImageLayout::eTransferDstOptimal, {copy_region});

    vk::ImageMemoryBarrier final_barrier{};
    final_barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    final_barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    final_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    final_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    final_barrier.image = *image_;
    final_barrier.subresourceRange = barrier.subresourceRange;

    command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, 
        {}, {}, {}, {final_barrier});

    command_buffer.end();

    // Submit command buffer
    vk::SubmitInfo submit_info{};
    submit_info.commandBufferCount = 1;
    vk::CommandBuffer raw_cmd = command_buffer;
    submit_info.pCommandBuffers = &raw_cmd;

    ctx.GraphicsQueue().submit(submit_info, nullptr);
    ctx.GraphicsQueue().waitIdle();

    staging_buffer.Shutdown(ctx);
}

void VkTexture::UpdateCubeFaceData(VkContext& ctx,
                                   void const* data,
                                   uint32_t size,
                                   uint32_t face_index,
                                   uint32_t mip_level) {
    if (face_index >= 6) {
        return;
    }
    UpdateData(ctx, data, size, mip_level, face_index);
}

} // namespace vkfw
