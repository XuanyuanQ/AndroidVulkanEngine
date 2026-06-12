cmake_minimum_required(VERSION 3.20)

project(AndroidVulkanEngineEvaluation LANGUAGES CXX)

option(AVE_BUILD_EXAMPLES "Build evaluation examples" ON)

find_package(Threads REQUIRED)
find_package(Vulkan QUIET)
find_package(glfw3 QUIET)

add_library(ave_engine STATIC
    src/core/Engine.cpp
    src/core/JobSystem.cpp
    src/core/Profiler.cpp
    src/project/XmlSceneLoader.cpp
    src/game/GameObject.cpp
    src/scene/SceneWorld.cpp
    src/ui/UIRuntime.cpp
    src/resource/AssetManager.cpp
    src/resource/ResourceSystem.cpp
    external/stb_image/stb_impl.cpp
    src/render/FrameGraph.cpp
    src/render/Renderer.cpp
    src/render/MaterialSystem.cpp
    src/render/RenderPassCommon.cpp
    src/render/DepthPrepass.cpp
    src/render/ShadowPass.cpp
    src/render/SkyboxPass.cpp
    src/render/PBRPass.cpp
    src/render/ComputePass.cpp
    src/render/UIPass.cpp
    src/render/ToneMappingPass.cpp
    src/render/PassDataView.cpp
    src/render/PipelineSystem.cpp
    src/render/AndroidSurfaceRenderBackend.cpp
    src/xr/OpenXRRenderBackend.cpp
    src/xr/OpenXRRuntime.cpp
    # src/rhi/VulkanDevice.cpp
)

target_include_directories(ave_engine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/glm-0.9.9.5"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/stb_image"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/runtime/android/cpp"
)

target_compile_features(ave_engine PUBLIC cxx_std_20)
target_compile_definitions(ave_engine PUBLIC
    NOMINMAX
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)

if (MSVC)
    target_compile_options(ave_engine PRIVATE /FS)
endif()

target_link_libraries(ave_engine
    PUBLIC
        Threads::Threads
)

if (Vulkan_FOUND)
    target_compile_definitions(ave_engine PUBLIC AVE_HAS_VULKAN=1)
    target_link_libraries(ave_engine PUBLIC Vulkan::Vulkan)
    if (glfw3_FOUND)
        target_link_libraries(ave_engine PUBLIC glfw)
    endif()
    target_sources(ave_engine PRIVATE
        src/rhi/core/VkBuffer.cpp
        src/rhi/core/VkCommandBuffer.cpp
        src/rhi/core/VkContext.cpp
        src/rhi/core/VkDescriptor.cpp
        src/rhi/core/VkFrameSync.cpp
        src/rhi/core/VkMemory.cpp
        src/rhi/core/VkPipeline.cpp
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

if (AVE_BUILD_EXAMPLES AND Vulkan_FOUND AND glfw3_FOUND)
    add_executable(ave_preview examples/preview/main.cpp)
    target_link_libraries(ave_preview PRIVATE ave_engine glfw)
    target_include_directories(ave_preview PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/runtime/android/cpp"
    )
elseif(AVE_BUILD_EXAMPLES)
    message(STATUS "ave_preview skipped: Vulkan and glfw3 are required")
endif()
