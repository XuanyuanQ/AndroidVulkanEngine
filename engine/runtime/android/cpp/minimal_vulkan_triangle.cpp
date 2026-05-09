#define VK_USE_PLATFORM_ANDROID_KHR

#include "minimal_vulkan_triangle.h"

#include "ave/project/XmlSceneLoader.h"

#include <android/log.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace ave::android {

namespace {

constexpr char kLogTag[] = "AveRuntime";

void logInfo(char const* message)
{
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
}

void logError(char const* message)
{
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}

bool check(VkResult result, char const* message)
{
    if (result != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s: VkResult=%d", message, result);
        return false;
    }
    return true;
}

VkSurfaceFormatKHR chooseSurfaceFormat(std::vector<VkSurfaceFormatKHR> const& formats)
{
    for (auto const& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(std::vector<VkPresentModeKHR> const& modes)
{
    for (auto const mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

bool MinimalVulkanTriangle::create(AAssetManager* assets, std::string project_path)
{
    assets_ = assets;
    project_path_ = std::move(project_path);
    logProjectAsset();
    return createInstance();
}

void MinimalVulkanTriangle::destroy()
{
    clearSurface();
    cleanupDeviceResources();
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    logInfo("Ave runtime destroyed.");
}

void MinimalVulkanTriangle::setSurface(ANativeWindow* window)
{
    clearSurface();
    window_ = window;
    if (window_ != nullptr) {
        ANativeWindow_acquire(window_);
    }

    if (window_ == nullptr) {
        return;
    }

    if (!createSurface() || !selectPhysicalDevice() || !createDevice() || !loadSceneMesh() ||
        !createVertexBuffer() || !createSwapchain() || !createRenderPass() || !createPipeline() || !createFramebuffers() ||
        !createCommandPoolAndBuffers() || !createSyncObjects()) {
        logError("Failed to initialize Vulkan triangle renderer.");
        return;
    }

    drawFrame();
}

void MinimalVulkanTriangle::clearSurface()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    cleanupDeviceResources();
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (window_ != nullptr) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

void MinimalVulkanTriangle::resize(int width, int height)
{
    width_ = width;
    height_ = height;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Surface resized: %dx%d", width_, height_);
    if (device_ != VK_NULL_HANDLE && window_ != nullptr) {
        drawFrame();
    }
}

bool MinimalVulkanTriangle::createInstance()
{
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "AveTriangleGame";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "AveEngine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    std::array<char const*, 2> extensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    return check(vkCreateInstance(&create_info, nullptr, &instance_), "vkCreateInstance failed");
}

bool MinimalVulkanTriangle::createSurface()
{
    VkAndroidSurfaceCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    create_info.window = window_;
    return check(vkCreateAndroidSurfaceKHR(instance_, &create_info, nullptr, &surface_), "vkCreateAndroidSurfaceKHR failed");
}

bool MinimalVulkanTriangle::selectPhysicalDevice()
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (device_count == 0) {
        logError("No Vulkan physical devices found.");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    for (auto const device : devices) {
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queues.data());

        for (uint32_t index = 0; index < queue_count; ++index) {
            VkBool32 present_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface_, &present_supported);
            if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_supported) {
                physical_device_ = device;
                graphics_queue_family_ = index;
                return true;
            }
        }
    }

    logError("No graphics+present queue family found.");
    return false;
}

bool MinimalVulkanTriangle::createDevice()
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    char const* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = extensions;

    if (!check(vkCreateDevice(physical_device_, &create_info, nullptr, &device_), "vkCreateDevice failed")) {
        return false;
    }

    vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
    return true;
}

bool MinimalVulkanTriangle::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());
    VkSurfaceFormatKHR const surface_format = chooseSurfaceFormat(formats);

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, present_modes.data());

    swapchain_extent_ = capabilities.currentExtent;
    if (swapchain_extent_.width == std::numeric_limits<uint32_t>::max()) {
        swapchain_extent_.width = std::clamp<uint32_t>(static_cast<uint32_t>(std::max(width_, 1)), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        swapchain_extent_.height = std::clamp<uint32_t>(static_cast<uint32_t>(std::max(height_, 1)), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface_;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = swapchain_extent_;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = choosePresentMode(present_modes);
    create_info.clipped = VK_TRUE;

    if (!check(vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_), "vkCreateSwapchainKHR failed")) {
        return false;
    }

    swapchain_format_ = surface_format.format;
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

    swapchain_image_views_.reserve(swapchain_images_.size());
    for (auto const image : swapchain_images_) {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = swapchain_format_;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;

        VkImageView view = VK_NULL_HANDLE;
        if (!check(vkCreateImageView(device_, &view_info, nullptr, &view), "vkCreateImageView failed")) {
            return false;
        }
        swapchain_image_views_.push_back(view);
    }

    return true;
}

bool MinimalVulkanTriangle::createRenderPass()
{
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swapchain_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &color_attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    return check(vkCreateRenderPass(device_, &create_info, nullptr, &render_pass_), "vkCreateRenderPass failed");
}

bool MinimalVulkanTriangle::loadSceneMesh()
{
    vertices_.clear();

    ave::project::XmlSceneLoader loader;
    auto const project_text = readTextAsset(project_path_.c_str());
    auto const project = loader.LoadProjectText(project_text);
    auto const scene_text = readTextAsset(project.entry_scene.c_str());
    auto const scene = loader.LoadSceneText(scene_text);

    for (auto const& object : scene.objects) {
        if (!object.has_mesh) {
            continue;
        }

        for (auto const& source : object.mesh.vertices) {
            Vertex vertex{};
            std::copy(source.position.begin(), source.position.end(), vertex.position);
            std::copy(source.color.begin(), source.color.end(), vertex.color);
            vertices_.push_back(vertex);
        }
    }

    if (vertices_.size() < 3) {
        logError("Scene XML must define at least three <Vertex> entries.");
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Loaded %zu XML-defined vertices from %s", vertices_.size(), project.entry_scene.c_str());
    return true;
}

bool MinimalVulkanTriangle::createVertexBuffer()
{
    VkDeviceSize const buffer_size = sizeof(Vertex) * vertices_.size();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(device_, &buffer_info, nullptr, &vertex_buffer_), "vkCreateBuffer vertex failed")) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, vertex_buffer_, &requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (!check(vkAllocateMemory(device_, &alloc_info, nullptr, &vertex_memory_), "vkAllocateMemory vertex failed")) {
        return false;
    }

    void* mapped = nullptr;
    vkMapMemory(device_, vertex_memory_, 0, buffer_size, 0, &mapped);
    std::memcpy(mapped, vertices_.data(), static_cast<size_t>(buffer_size));
    vkUnmapMemory(device_, vertex_memory_);

    return check(vkBindBufferMemory(device_, vertex_buffer_, vertex_memory_, 0), "vkBindBufferMemory vertex failed");
}

bool MinimalVulkanTriangle::createPipeline()
{
    auto vert_code = readShaderAsset("compiled_shaders/solid_triangle.vert.spv");
    auto frag_code = readShaderAsset("compiled_shaders/solid_triangle.frag.spv");
    if (vert_code.empty() || frag_code.empty()) {
        logError("Missing compiled triangle shaders.");
        return false;
    }

    VkShaderModuleCreateInfo vert_info{};
    vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_info.codeSize = vert_code.size() * sizeof(uint32_t);
    vert_info.pCode = vert_code.data();

    VkShaderModuleCreateInfo frag_info = vert_info;
    frag_info.codeSize = frag_code.size() * sizeof(uint32_t);
    frag_info.pCode = frag_code.data();

    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;
    if (!check(vkCreateShaderModule(device_, &vert_info, nullptr, &vert_module), "vkCreateShaderModule vert failed") ||
        !check(vkCreateShaderModule(device_, &frag_info, nullptr, &frag_module), "vkCreateShaderModule frag failed")) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, position);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchain_extent_.width);
    viewport.height = static_cast<float>(swapchain_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = swapchain_extent_;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (!check(vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_), "vkCreatePipelineLayout failed")) {
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = render_pass_;
    pipeline_info.subpass = 0;

    bool const ok = check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_), "vkCreateGraphicsPipelines failed");
    vkDestroyShaderModule(device_, frag_module, nullptr);
    vkDestroyShaderModule(device_, vert_module, nullptr);
    return ok;
}

bool MinimalVulkanTriangle::createFramebuffers()
{
    framebuffers_.reserve(swapchain_image_views_.size());
    for (auto const view : swapchain_image_views_) {
        VkFramebufferCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        create_info.renderPass = render_pass_;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &view;
        create_info.width = swapchain_extent_.width;
        create_info.height = swapchain_extent_.height;
        create_info.layers = 1;

        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (!check(vkCreateFramebuffer(device_, &create_info, nullptr, &framebuffer), "vkCreateFramebuffer failed")) {
            return false;
        }
        framebuffers_.push_back(framebuffer);
    }
    return true;
}

bool MinimalVulkanTriangle::createCommandPoolAndBuffers()
{
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = graphics_queue_family_;
    if (!check(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_), "vkCreateCommandPool failed")) {
        return false;
    }

    command_buffers_.resize(framebuffers_.size());
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = static_cast<uint32_t>(command_buffers_.size());
    return check(vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_.data()), "vkAllocateCommandBuffers failed");
}

bool MinimalVulkanTriangle::createSyncObjects()
{
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    return check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &image_available_), "vkCreateSemaphore image failed") &&
           check(vkCreateSemaphore(device_, &semaphore_info, nullptr, &render_finished_), "vkCreateSemaphore render failed") &&
           check(vkCreateFence(device_, &fence_info, nullptr, &in_flight_), "vkCreateFence failed");
}

void MinimalVulkanTriangle::recordCommandBuffer(VkCommandBuffer command_buffer, uint32_t image_index)
{
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(command_buffer, &begin_info);

    VkClearValue clear{};
    clear.color = {{0.03f, 0.04f, 0.06f, 1.0f}};

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = render_pass_;
    render_pass_info.framebuffer = framebuffers_[image_index];
    render_pass_info.renderArea.extent = swapchain_extent_;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear;

    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer_, &offset);
    vkCmdDraw(command_buffer, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);
    vkEndCommandBuffer(command_buffer);
}

void MinimalVulkanTriangle::drawFrame()
{
    if (device_ == VK_NULL_HANDLE || swapchain_ == VK_NULL_HANDLE || command_buffers_.empty()) {
        return;
    }

    vkWaitForFences(device_, 1, &in_flight_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &in_flight_);

    uint32_t image_index = 0;
    VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_, VK_NULL_HANDLE, &image_index);
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "vkAcquireNextImageKHR failed: %d", acquire);
        return;
    }

    vkResetCommandBuffer(command_buffers_[image_index], 0);
    recordCommandBuffer(command_buffers_[image_index], image_index);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_available_;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers_[image_index];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_finished_;

    if (!check(vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_), "vkQueueSubmit failed")) {
        return;
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &image_index;
    VkResult present = vkQueuePresentKHR(graphics_queue_, &present_info);
    if (present != VK_SUCCESS && present != VK_SUBOPTIMAL_KHR) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "vkQueuePresentKHR failed: %d", present);
    }
}

void MinimalVulkanTriangle::cleanupSurfaceResources()
{
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    if (in_flight_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, in_flight_, nullptr);
        in_flight_ = VK_NULL_HANDLE;
    }
    if (render_finished_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, render_finished_, nullptr);
        render_finished_ = VK_NULL_HANDLE;
    }
    if (image_available_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, image_available_, nullptr);
        image_available_ = VK_NULL_HANDLE;
    }
    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
        command_buffers_.clear();
    }
    for (auto const framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    if (vertex_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, vertex_buffer_, nullptr);
        vertex_buffer_ = VK_NULL_HANDLE;
    }
    if (vertex_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, vertex_memory_, nullptr);
        vertex_memory_ = VK_NULL_HANDLE;
    }
    for (auto const view : swapchain_image_views_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchain_image_views_.clear();
    swapchain_images_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void MinimalVulkanTriangle::cleanupDeviceResources()
{
    cleanupSurfaceResources();
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    physical_device_ = VK_NULL_HANDLE;
    graphics_queue_family_ = UINT32_MAX;
    graphics_queue_ = VK_NULL_HANDLE;
}

void MinimalVulkanTriangle::logProjectAsset() const
{
    if (assets_ == nullptr) {
        return;
    }

    AAsset* project = AAssetManager_open(assets_, project_path_.c_str(), AASSET_MODE_BUFFER);
    if (project != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Loaded project asset: %s (%ld bytes)", project_path_.c_str(), static_cast<long>(AAsset_getLength(project)));
        AAsset_close(project);
    } else {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Project asset not found: %s", project_path_.c_str());
    }
}

std::vector<uint32_t> MinimalVulkanTriangle::readShaderAsset(char const* path) const
{
    if (assets_ == nullptr) {
        return {};
    }

    AAsset* asset = AAssetManager_open(assets_, path, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Shader asset not found: %s", path);
        return {};
    }

    size_t const size = static_cast<size_t>(AAsset_getLength(asset));
    std::vector<uint32_t> words((size + sizeof(uint32_t) - 1) / sizeof(uint32_t));
    int const read = AAsset_read(asset, words.data(), size);
    AAsset_close(asset);

    if (read < 0 || static_cast<size_t>(read) != size || size % sizeof(uint32_t) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Invalid SPIR-V asset: %s", path);
        return {};
    }

    return words;
}

std::string MinimalVulkanTriangle::readTextAsset(char const* path) const
{
    if (assets_ == nullptr) {
        return {};
    }

    AAsset* asset = AAssetManager_open(assets_, path, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Text asset not found: %s", path);
        return {};
    }

    size_t const size = static_cast<size_t>(AAsset_getLength(asset));
    std::string text(size, '\0');
    int const read = AAsset_read(asset, text.data(), size);
    AAsset_close(asset);
    if (read < 0) {
        return {};
    }
    text.resize(static_cast<size_t>(read));
    return text;
}

uint32_t MinimalVulkanTriangle::findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);

    for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
        if ((type_filter & (1u << index)) &&
            (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }

    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Failed to find suitable Vulkan memory type.");
    return 0;
}

} // namespace ave::android
