@echo off
"D:\\Setup\\android_sdk\\cmake\\3.22.1\\bin\\cmake.exe" ^
  "-HD:\\Code\\ForestRendering\\evaluation\\AndroidVulkanEngine\\sample\\TriangleGame\\build\\android\\app\\src\\main\\cpp" ^
  "-DCMAKE_SYSTEM_NAME=Android" ^
  "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" ^
  "-DCMAKE_SYSTEM_VERSION=26" ^
  "-DANDROID_PLATFORM=android-26" ^
  "-DANDROID_ABI=armeabi-v7a" ^
  "-DCMAKE_ANDROID_ARCH_ABI=armeabi-v7a" ^
  "-DANDROID_NDK=D:\\Setup\\android_sdk\\ndk\\26.1.10909125" ^
  "-DCMAKE_ANDROID_NDK=D:\\Setup\\android_sdk\\ndk\\26.1.10909125" ^
  "-DCMAKE_TOOLCHAIN_FILE=D:\\Setup\\android_sdk\\ndk\\26.1.10909125\\build\\cmake\\android.toolchain.cmake" ^
  "-DCMAKE_MAKE_PROGRAM=D:\\Setup\\android_sdk\\cmake\\3.22.1\\bin\\ninja.exe" ^
  "-DCMAKE_CXX_FLAGS=-std=c++20 -Wall -Wextra" ^
  "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=D:\\Code\\ForestRendering\\evaluation\\AndroidVulkanEngine\\sample\\TriangleGame\\build\\android\\app\\build\\intermediates\\cxx\\Debug\\1z1t1e2o\\obj\\armeabi-v7a" ^
  "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=D:\\Code\\ForestRendering\\evaluation\\AndroidVulkanEngine\\sample\\TriangleGame\\build\\android\\app\\build\\intermediates\\cxx\\Debug\\1z1t1e2o\\obj\\armeabi-v7a" ^
  "-DCMAKE_BUILD_TYPE=Debug" ^
  "-BD:\\Code\\ForestRendering\\evaluation\\AndroidVulkanEngine\\sample\\TriangleGame\\build\\android\\app\\.cxx\\Debug\\1z1t1e2o\\armeabi-v7a" ^
  -GNinja ^
  "-DANDROID_STL=c++_shared"
