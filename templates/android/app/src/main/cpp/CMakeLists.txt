cmake_minimum_required(VERSION 3.22)

project(ave_runtime LANGUAGES CXX)

set(AVE_ENGINE_ROOT "__AVE_ENGINE_ROOT__" CACHE PATH "Ave engine root")
set(AVE_ANDROID_RUNTIME "${AVE_ENGINE_ROOT}/engine/runtime/android/cpp")

# 配置本地 Vulkan 库路径
set(VULKAN_SDK_PATH "$ENV{VULKAN_SDK}" CACHE PATH "Vulkan SDK path")
if(VULKAN_SDK_PATH)
   find_package(Vulkan REQUIRED)
   set(VULKAN_LIBRARY "Vulkan::Vulkan")
else()
    # 使用系统 Vulkan 库
    set(VULKAN_LIBRARY "Vulkan")
    message(STATUS "Using system Vulkan library")
endif()

find_path(VULKAN_HPP_INCLUDE_DIR
    NAMES vulkan/vulkan_raii.hpp
    PATHS
        "$ENV{VULKAN_SDK}/Include"
        "$ENV{VULKAN_SDK}/include"
        /usr/local/include
        /opt/homebrew/include
    NO_CMAKE_FIND_ROOT_PATH
)

# file(GLOB CORE_SOURCES "${AVE_ENGINE_ROOT}/src/rhi/core/*.cpp")
file(GLOB RHI_SOURCES "${AVE_ENGINE_ROOT}/src/rhi/core/*.cpp")
add_library(ave_runtime SHARED
    "${AVE_ANDROID_RUNTIME}/native_entry.cpp"
    "${AVE_ANDROID_RUNTIME}/minimal_vulkan_triangle.cpp"
    "${AVE_ENGINE_ROOT}/src/core/JobSystem.cpp"
    "${AVE_ENGINE_ROOT}/src/project/XmlSceneLoader.cpp"
    "${AVE_ENGINE_ROOT}/src/resource/ResourceSystem.cpp"
    "${AVE_ENGINE_ROOT}/src/scene/SceneWorld.cpp"
    "${AVE_ENGINE_ROOT}/src/ui/UIRuntime.cpp"
    "${AVE_ENGINE_ROOT}/src/render/PassDataView.cpp"
    "${AVE_ENGINE_ROOT}/src/render/RenderPassCommon.cpp"
    "${AVE_ENGINE_ROOT}/src/render/DepthPrepass.cpp"
    "${AVE_ENGINE_ROOT}/src/render/ShadowPass.cpp"
    "${AVE_ENGINE_ROOT}/src/render/SkyboxPass.cpp"
    "${AVE_ENGINE_ROOT}/src/render/PBRPass.cpp"
    "${AVE_ENGINE_ROOT}/src/render/ComputePass.cpp"
    "${AVE_ENGINE_ROOT}/src/render/UIPass.cpp"
    "${AVE_ENGINE_ROOT}/src/render/ToneMappingPass.cpp"
    "${AVE_ENGINE_ROOT}/src/render/FrameGraph.cpp"
    "${AVE_ENGINE_ROOT}/src/render/PipelineSystem.cpp"
    "${AVE_ENGINE_ROOT}/src/render/Renderer.cpp"
    "${AVE_ENGINE_ROOT}/src/render/AndroidSurfaceRenderBackend.cpp"
    "${AVE_ENGINE_ROOT}/src/render/MaterialSystem.cpp"
    "${AVE_ENGINE_ROOT}/src/xr/OpenXRRenderBackend.cpp"
    "${AVE_ENGINE_ROOT}/src/xr/OpenXRRuntime.cpp"
    "${AVE_ENGINE_ROOT}/external/stb_image/stb_impl.cpp"
    "${RHI_SOURCES}"
)

target_compile_features(ave_runtime PRIVATE cxx_std_20)

target_include_directories(ave_runtime
    PRIVATE
        "${AVE_ANDROID_RUNTIME}"
        "${AVE_ENGINE_ROOT}/include"
        "${AVE_ENGINE_ROOT}/src/rhi/core"
        "${AVE_ENGINE_ROOT}/external/glm-0.9.9.5"
        "${AVE_ENGINE_ROOT}/external/stb_image"
        "${AVE_ENGINE_ROOT}/external/openxr/include"
)

# 添加本地 Vulkan 头文件路径
if(VULKAN_HPP_INCLUDE_DIR)
    target_include_directories(ave_runtime PRIVATE "${VULKAN_HPP_INCLUDE_DIR}")
endif()

set(OPENXR_LOADER_PATH "${AVE_ENGINE_ROOT}/external/openxr/lib/android/${ANDROID_ABI}/libopenxr_loader.so")
if(EXISTS "${OPENXR_LOADER_PATH}")
    add_library(openxr_loader SHARED IMPORTED)
    set_target_properties(openxr_loader PROPERTIES
        IMPORTED_LOCATION "${OPENXR_LOADER_PATH}"
    )
    set(OPENXR_LOADER_TARGET openxr_loader)
    message(STATUS "Using bundled OpenXR loader: ${OPENXR_LOADER_PATH}")
else()
    set(OPENXR_LOADER_TARGET "")
    message(WARNING "Bundled OpenXR loader not found: ${OPENXR_LOADER_PATH}")
endif()

target_link_libraries(ave_runtime
    PRIVATE
        android
        log
        dl
        ${OPENXR_LOADER_TARGET}
        ${VULKAN_LIBRARY}
)

add_definitions(-DNOMINMAX)
# 5. 添加必要的宏定义（针对 Vulkan-Hpp）
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
