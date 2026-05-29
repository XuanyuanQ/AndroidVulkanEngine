#include "minimal_vulkan_triangle.h"

#include <android/asset_manager_jni.h>
#include <android/keycodes.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <memory>
#include <unordered_map>

static JavaVM* g_vm = nullptr;
static jclass g_ave_activity_class = nullptr;
static jmethodID g_instantiate_script_mid = nullptr;
static jmethodID g_update_scripts_mid = nullptr;
static jmethodID g_trigger_script_mid = nullptr;
static jmethodID g_clear_scripts_mid = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        jclass local_class = env->FindClass("com/ave/engine/AveActivity");
        g_ave_activity_class = static_cast<jclass>(env->NewGlobalRef(local_class));

        g_instantiate_script_mid = env->GetStaticMethodID(
            g_ave_activity_class,
            "jniInstantiateScript",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V");
        g_update_scripts_mid = env->GetStaticMethodID(
            g_ave_activity_class, "jniUpdateScripts", "(F)V");
        g_trigger_script_mid = env->GetStaticMethodID(
            g_ave_activity_class, "jniTriggerScriptMethod", "(Ljava/lang/String;Ljava/lang/String;)V");
        g_clear_scripts_mid = env->GetStaticMethodID(
            g_ave_activity_class, "jniClearScripts", "()V");
    }
    return JNI_VERSION_1_6;
}

namespace ave::android {

JavaVM* GetJavaVM() { return g_vm; }

JNIEnv* GetJniEnv() {
    JNIEnv* env = nullptr;
    if (g_vm == nullptr) {
        return nullptr;
    }
    jint res = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != 0) {
            return nullptr;
        }
    }
    return env;
}

void Jni_InstantiateScript(std::string const& object_id,
                           std::string const& java_class,
                           std::string const& target_object_id,
                           std::unordered_map<std::string, std::string> const& parameters) {
    JNIEnv* env = GetJniEnv();
    if (!env || !g_instantiate_script_mid) {
        return;
    }

    jstring j_object_id = env->NewStringUTF(object_id.c_str());
    jstring j_java_class = env->NewStringUTF(java_class.c_str());
    jstring j_target_object_id = env->NewStringUTF(target_object_id.c_str());

    jclass string_class = env->FindClass("java/lang/String");
    jobjectArray j_keys = env->NewObjectArray(static_cast<jsize>(parameters.size()), string_class, nullptr);
    jobjectArray j_values = env->NewObjectArray(static_cast<jsize>(parameters.size()), string_class, nullptr);

    jsize index = 0;
    for (auto const& [key, value] : parameters) {
        jstring j_key = env->NewStringUTF(key.c_str());
        jstring j_value = env->NewStringUTF(value.c_str());
        env->SetObjectArrayElement(j_keys, index, j_key);
        env->SetObjectArrayElement(j_values, index, j_value);
        env->DeleteLocalRef(j_key);
        env->DeleteLocalRef(j_value);
        ++index;
    }

    env->CallStaticVoidMethod(
        g_ave_activity_class,
        g_instantiate_script_mid,
        j_object_id,
        j_java_class,
        j_target_object_id,
        j_keys,
        j_values);

    env->DeleteLocalRef(j_object_id);
    env->DeleteLocalRef(j_java_class);
    env->DeleteLocalRef(j_target_object_id);
    env->DeleteLocalRef(j_keys);
    env->DeleteLocalRef(j_values);
    env->DeleteLocalRef(string_class);
}

void Jni_UpdateScripts(float dt) {
    JNIEnv* env = GetJniEnv();
    if (!env || !g_update_scripts_mid) {
        return;
    }
    env->CallStaticVoidMethod(g_ave_activity_class, g_update_scripts_mid, static_cast<jfloat>(dt));
}

void Jni_TriggerScriptMethod(std::string const& target, std::string const& method) {
    JNIEnv* env = GetJniEnv();
    if (!env || !g_trigger_script_mid) {
        return;
    }

    jstring j_target = env->NewStringUTF(target.c_str());
    jstring j_method = env->NewStringUTF(method.c_str());
    env->CallStaticVoidMethod(g_ave_activity_class, g_trigger_script_mid, j_target, j_method);
    env->DeleteLocalRef(j_target);
    env->DeleteLocalRef(j_method);
}

void Jni_ClearScripts() {
    JNIEnv* env = GetJniEnv();
    if (!env || !g_clear_scripts_mid) {
        return;
    }
    env->CallStaticVoidMethod(g_ave_activity_class, g_clear_scripts_mid);
}

std::unique_ptr<MinimalVulkanTriangle> g_runtime;

} // namespace ave::android

using namespace ave::android;

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeCreate(JNIEnv* env, jclass, jobject asset_manager, jstring project_path) {
    char const* chars = env->GetStringUTFChars(project_path, nullptr);
    std::string path = chars != nullptr ? chars : "project.xml";
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(project_path, chars);
    }

    g_runtime = std::make_unique<ave::android::MinimalVulkanTriangle>();
    g_runtime->create(AAssetManager_fromJava(env, asset_manager), path);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeDestroy(JNIEnv*, jclass) {
    if (g_runtime) {
        g_runtime->destroy();
        g_runtime.reset();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeKeyEvent(JNIEnv*, jclass, jint key_code, jint action) {
    if (!g_runtime) {
        return;
    }
    if (action == 0) {
        g_runtime->setKeyState(key_code, true);
    } else if (action == 1) {
        g_runtime->setKeyState(key_code, false);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeTouchEvent(JNIEnv*, jclass, jfloat x, jfloat y, jint action) {
    if (g_runtime) {
        g_runtime->onTouchEvent(x, y, action);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectPosition(
    JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z) {
    if (!g_runtime || !object_id) {
        return;
    }
    char const* chars = env->GetStringUTFChars(object_id, nullptr);
    if (!chars) {
        return;
    }
    g_runtime->setObjectPosition(chars, x, y, z);
    env->ReleaseStringUTFChars(object_id, chars);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectRotation(
    JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z) {
    if (!g_runtime || !object_id) {
        return;
    }
    char const* chars = env->GetStringUTFChars(object_id, nullptr);
    if (!chars) {
        return;
    }
    g_runtime->setObjectRotation(chars, x, y, z);
    env->ReleaseStringUTFChars(object_id, chars);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectScale(
    JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z) {
    if (!g_runtime || !object_id) {
        return;
    }
    char const* chars = env->GetStringUTFChars(object_id, nullptr);
    if (!chars) {
        return;
    }
    g_runtime->setObjectScale(chars, x, y, z);
    env->ReleaseStringUTFChars(object_id, chars);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectVisible(
    JNIEnv* env, jclass, jstring object_id, jboolean visible) {
    if (!g_runtime || !object_id) {
        return;
    }
    char const* chars = env->GetStringUTFChars(object_id, nullptr);
    if (!chars) {
        return;
    }
    g_runtime->setObjectVisible(chars, visible == JNI_TRUE);
    env->ReleaseStringUTFChars(object_id, chars);
}

extern "C" JNIEXPORT jboolean JNICALL // 修改返回类型为 jboolean
Java_com_ave_engine_AveActivity_nativeGetObjectVisible(
    JNIEnv* env, jclass, jstring object_id) {
    
    // 如果运行时不存在或 id 为空，默认返回 false
    if (!g_runtime || !object_id) {
        return JNI_FALSE; 
    }
    
    char const* chars = env->GetStringUTFChars(object_id, nullptr);
    if (!chars) {
        return JNI_FALSE;
    }
    
    // 定义一个临时的 C++ bool 变量来接收结果
    bool visible_result = false;
    g_runtime->GetObjectVisible(chars, visible_result);
    
    // 释放字符串内存
    env->ReleaseStringUTFChars(object_id, chars);
    
    // 将 C++ bool 转换为 jboolean 返回给 Java
    return visible_result ? JNI_TRUE : JNI_FALSE;
}


extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectColor(
    JNIEnv* env, jclass, jstring object_id, jfloat r, jfloat g, jfloat b, jfloat a) {
    if (!g_runtime || !object_id) {
        return;
    }
    char const* chars = env->GetStringUTFChars(object_id, nullptr);
    if (!chars) {
        return;
    }
    g_runtime->setObjectColor(chars, r, g, b, a);
    env->ReleaseStringUTFChars(object_id, chars);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetSurface(JNIEnv* env, jclass, jobject surface) {
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
Java_com_ave_engine_AveActivity_nativeClearSurface(JNIEnv*, jclass) {
    if (g_runtime) {
        g_runtime->clearSurface();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeResize(JNIEnv*, jclass, jint width, jint height) {
    if (g_runtime) {
        g_runtime->resize(width, height);
    }
}
