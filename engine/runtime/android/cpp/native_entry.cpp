#include "minimal_vulkan_triangle.h"

#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <memory>
#include <unordered_map>
#include <android/keycodes.h> // 提供 AKEYCODE_W 等宏

namespace {
std::unique_ptr<ave::android::MinimalVulkanTriangle> g_runtime;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeCreate(JNIEnv* env, jclass, jobject asset_manager, jstring project_path)
{
    char const* chars = env->GetStringUTFChars(project_path, nullptr);
    std::string path = chars != nullptr ? chars : "project.xml";
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(project_path, chars);
    }

    g_runtime = std::make_unique<ave::android::MinimalVulkanTriangle>();
    g_runtime->create(AAssetManager_fromJava(env, asset_manager), path);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeDestroy(JNIEnv*, jclass)
{
    if (g_runtime) {
        g_runtime->destroy();
        g_runtime.reset();
    }
}
extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeKeyEvent(JNIEnv* env, jclass, jint key_code, jint action) {
    if (action == 0) {
        g_runtime->setKeyState(key_code, true);  // 按下时：g_key_states[29] 变成了 true
    } else if (action == 1) {
        g_runtime->setKeyState(key_code, false); // 抬起时：g   _key_states[29] 又变回了 false
    }
}
extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeMotionEvent(JNIEnv* env, jclass clazz, jfloat dx, jfloat dy, jint action) 
{
    g_runtime->setMotionState(dx, dy); // 累加鼠标偏移量，供 C++ 侧查询
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetSurface(JNIEnv* env, jclass, jobject surface)
{
    if (!g_runtime) {
        return;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    g_runtime->setSurface(window);
    if (window != nullptr) {
        ANativeWindow_release(window);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeClearSurface(JNIEnv*, jclass)
{
    if (g_runtime) {
        g_runtime->clearSurface();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeResize(JNIEnv*, jclass, jint width, jint height)
{
    if (g_runtime) {
        g_runtime->resize(width, height);
    }
}
