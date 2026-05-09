cmake_minimum_required(VERSION 3.22)

project(ave_runtime LANGUAGES CXX)

set(AVE_ENGINE_ROOT "__AVE_ENGINE_ROOT__" CACHE PATH "Ave engine root")
set(AVE_ANDROID_RUNTIME "${AVE_ENGINE_ROOT}/engine/runtime/android/cpp")

add_library(ave_runtime SHARED
    "${AVE_ANDROID_RUNTIME}/native_entry.cpp"
    "${AVE_ANDROID_RUNTIME}/minimal_vulkan_triangle.cpp"
    "${AVE_ENGINE_ROOT}/src/project/XmlSceneLoader.cpp"
    "${AVE_ENGINE_ROOT}/src/rhi/VulkanDevice.cpp"
)

target_compile_features(ave_runtime PRIVATE cxx_std_20)

target_include_directories(ave_runtime
    PRIVATE
        "${AVE_ANDROID_RUNTIME}"
        "${AVE_ENGINE_ROOT}/include"
)

target_link_libraries(ave_runtime
    PRIVATE
        android
        log
        vulkan
)
