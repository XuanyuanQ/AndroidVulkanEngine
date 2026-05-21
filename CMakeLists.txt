cmake_minimum_required(VERSION 3.20)

project(AndroidVulkanEngineEvaluation LANGUAGES CXX)

option(AVE_BUILD_EXAMPLES "Build evaluation examples" ON)

find_package(Threads REQUIRED)
find_package(Vulkan QUIET)

add_library(ave_engine STATIC
    src/core/Engine.cpp
    src/core/JobSystem.cpp
    src/core/Profiler.cpp
    src/project/XmlSceneLoader.cpp
    src/game/GameObject.cpp
    src/scene/SceneWorld.cpp
    src/resource/AssetManager.cpp
    src/resource/GpuUploadQueue.cpp
    src/resource/ResourceSystem.cpp
    src/render/CommandRecorder.cpp
    src/render/FrameGraph.cpp
    src/render/Renderer.cpp
    src/render/RenderWorld.cpp
    src/render/MaterialSystem.cpp
    src/render/RenderPasses.cpp
    src/render/PassDataView.cpp
    src/render/RenderSceneBuilder.cpp
    src/render/PipelineSystem.cpp
    # src/rhi/VulkanDevice.cpp
)

target_include_directories(ave_engine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/glm-0.9.9.5"
)

target_compile_features(ave_engine PUBLIC cxx_std_20)

target_link_libraries(ave_engine
    PUBLIC
        Threads::Threads
)

if (Vulkan_FOUND)
    target_compile_definitions(ave_engine PUBLIC AVE_HAS_VULKAN=1)
    target_link_libraries(ave_engine PUBLIC Vulkan::Vulkan)
    target_sources(ave_engine PRIVATE
        src/rhi/core/VkBuffer.cpp
        src/rhi/core/VkCommandBuffer.cpp
        src/rhi/core/VkContext.cpp
        src/rhi/core/VkDescriptor.cpp
        src/rhi/core/VkFrameSync.cpp
        src/rhi/core/VkMemory.cpp
        src/rhi/core/VkPipeline.cpp
        src/rhi/core/VkRasterRenderer.cpp
        src/rhi/core/VkRenderPass.cpp
        src/rhi/core/VkShader.cpp
        src/rhi/core/VkSwapchain.cpp
        src/rhi/core/VkTexture.cpp
        src/rhi/core/VkFramebufferSet.cpp
        src/rhi/core/vk_dispatch.cpp
    )
else()
    target_compile_definitions(ave_engine PUBLIC AVE_HAS_VULKAN=0)
endif()

if (AVE_BUILD_EXAMPLES)
    add_executable(ave_desktop_stub examples/desktop_stub/main.cpp)
    target_link_libraries(ave_desktop_stub PRIVATE ave_engine)
endif()
