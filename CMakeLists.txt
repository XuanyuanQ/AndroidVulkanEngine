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
    src/render/RenderSceneBuilder.cpp
    src/render/PipelineSystem.cpp
    # src/rhi/VulkanDevice.cpp
)

target_include_directories(ave_engine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/core"
)

target_compile_features(ave_engine PUBLIC cxx_std_20)

target_link_libraries(ave_engine
    PUBLIC
        Threads::Threads
)

if (Vulkan_FOUND)
    target_compile_definitions(ave_engine PUBLIC AVE_HAS_VULKAN=1)
    target_link_libraries(ave_engine PUBLIC Vulkan::Vulkan)
else()
    target_compile_definitions(ave_engine PUBLIC AVE_HAS_VULKAN=0)
endif()

if (AVE_BUILD_EXAMPLES)
    add_executable(ave_desktop_stub examples/desktop_stub/main.cpp)
    target_link_libraries(ave_desktop_stub PRIVATE ave_engine)
endif()
