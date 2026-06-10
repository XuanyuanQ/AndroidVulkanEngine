#include "minimal_vulkan_triangle.h"
#include "LogUtil.h"

#include <android/asset_manager_jni.h>
#include <android/keycodes.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <memory>
#include <string>
#include <unordered_map>

static JavaVM* g_vm = nullptr;
static jclass g_ave_activity_class = nullptr;
static jmethodID g_instantiate_script_mid = nullptr;
static jmethodID g_update_scripts_mid = nullptr;
static jmethodID g_trigger_script_mid = nullptr;
static jmethodID g_trigger_script_value_mid = nullptr;
static jmethodID g_clear_scripts_mid = nullptr;
static jmethodID g_generate_font_atlas_mid = nullptr;
static jmethodID g_destroy_script_mid = nullptr;
static jobject g_application_context = nullptr;

namespace {

std::string ReadJString(JNIEnv* env, jstring value)
{
    if (!env || !value) {
        return {};
    }

    char const* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) {
        return {};
    }

    std::string result = chars;
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

jfloatArray MakeFloatArray(JNIEnv* env, float const* values, jsize count)
{
    jfloatArray array = env->NewFloatArray(count);
    if (array && values && count > 0) {
        env->SetFloatArrayRegion(array, 0, count, values);
    }
    return array;
}

jfloatArray MakeVec3Array(JNIEnv* env, glm::vec3 const& value)
{
    float values[3] = {value.x, value.y, value.z};
    return MakeFloatArray(env, values, 3);
}

jfloatArray MakeVec4Array(JNIEnv* env, glm::vec4 const& value)
{
    float values[4] = {value.r, value.g, value.b, value.a};
    return MakeFloatArray(env, values, 4);
}

bool CheckAndClearException(JNIEnv* env)
{
    if (env && env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }
    return false;
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*)
{
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        jclass local_class = env->FindClass("com/ave/engine/AveActivity");
        g_ave_activity_class = static_cast<jclass>(env->NewGlobalRef(local_class));

        g_instantiate_script_mid = env->GetStaticMethodID(
            g_ave_activity_class,
            "jniInstantiateScript",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V");
        g_update_scripts_mid = env->GetStaticMethodID(g_ave_activity_class, "jniUpdateScripts", "(F)V");
        g_trigger_script_mid = env->GetStaticMethodID(
            g_ave_activity_class, "jniTriggerScriptMethod", "(Ljava/lang/String;Ljava/lang/String;)V");
        g_trigger_script_value_mid = env->GetStaticMethodID(
            g_ave_activity_class,
            "jniTriggerScriptValueMethod",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;F)V");
        g_clear_scripts_mid = env->GetStaticMethodID(g_ave_activity_class, "jniClearScripts", "()V");
        g_generate_font_atlas_mid = env->GetStaticMethodID(g_ave_activity_class, "jniGenerateFontAtlas", "()V");
        g_destroy_script_mid = env->GetStaticMethodID(g_ave_activity_class, "jniDestroyScript", "(Ljava/lang/String;)V");
    }
    return JNI_VERSION_1_6;
}

namespace ave::android {

JavaVM* GetJavaVM() { return g_vm; }

JNIEnv* GetJniEnv()
{
    JNIEnv* env = nullptr;
    if (g_vm == nullptr) {
        return nullptr;
    }

    jint const res = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
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
                           std::unordered_map<std::string, std::string> const& parameters)
{
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
    CheckAndClearException(env);

    env->DeleteLocalRef(j_object_id);
    env->DeleteLocalRef(j_java_class);
    env->DeleteLocalRef(j_target_object_id);
    env->DeleteLocalRef(j_keys);
    env->DeleteLocalRef(j_values);
    env->DeleteLocalRef(string_class);
}

void Jni_UpdateScripts(float dt)
{
    JNIEnv* env = GetJniEnv();
    if (!env || !g_update_scripts_mid) {
        return;
    }
    if (env->PushLocalFrame(128) < 0) {
        return;
    }
    env->CallStaticVoidMethod(g_ave_activity_class, g_update_scripts_mid, static_cast<jfloat>(dt));
    CheckAndClearException(env);
    env->PopLocalFrame(nullptr);
}

void Jni_TriggerScriptMethod(std::string const& target, std::string const& method)
{
    LOGI("Jni_TriggerScriptMethod: target=%s, method=%s", target.c_str(), method.c_str());
    JNIEnv* env = GetJniEnv();
    if (!env || !g_trigger_script_mid) {
        LOGE("Jni_TriggerScriptMethod: env or g_trigger_script_mid is null");
        return;
    }

    jstring j_target = env->NewStringUTF(target.c_str());
    jstring j_method = env->NewStringUTF(method.c_str());
    env->CallStaticVoidMethod(g_ave_activity_class, g_trigger_script_mid, j_target, j_method);
    CheckAndClearException(env);
    env->DeleteLocalRef(j_target);
    env->DeleteLocalRef(j_method);
}

void Jni_TriggerScriptValueMethod(std::string const& target, std::string const& method, std::string const& source_id, float value)
{
    LOGI("Jni_TriggerScriptValueMethod: target=%s, method=%s, source=%s, value=%.4f",
         target.c_str(), method.c_str(), source_id.c_str(), value);
    JNIEnv* env = GetJniEnv();
    if (!env || !g_trigger_script_value_mid) {
        LOGE("Jni_TriggerScriptValueMethod: env or g_trigger_script_value_mid is null");
        return;
    }

    jstring j_target = env->NewStringUTF(target.c_str());
    jstring j_method = env->NewStringUTF(method.c_str());
    jstring j_source = env->NewStringUTF(source_id.c_str());
    env->CallStaticVoidMethod(g_ave_activity_class, g_trigger_script_value_mid, j_target, j_method, j_source, static_cast<jfloat>(value));
    CheckAndClearException(env);
    env->DeleteLocalRef(j_target);
    env->DeleteLocalRef(j_method);
    env->DeleteLocalRef(j_source);
}

void Jni_ClearScripts()
{
    JNIEnv* env = GetJniEnv();
    if (!env || !g_clear_scripts_mid) {
        return;
    }
    env->CallStaticVoidMethod(g_ave_activity_class, g_clear_scripts_mid);
    CheckAndClearException(env);
}

void Jni_GenerateFontAtlas()
{
    JNIEnv* env = GetJniEnv();
    if (!env || !g_ave_activity_class || !g_generate_font_atlas_mid) {
        return;
    }
    env->CallStaticVoidMethod(g_ave_activity_class, g_generate_font_atlas_mid);
    CheckAndClearException(env);
}

void Jni_DestroyScript(std::string const& object_id)
{
    JNIEnv* env = GetJniEnv();
    if (!env || !g_destroy_script_mid) {
        return;
    }
    jstring j_object_id = env->NewStringUTF(object_id.c_str());
    env->CallStaticVoidMethod(g_ave_activity_class, g_destroy_script_mid, j_object_id);
    CheckAndClearException(env);
    env->DeleteLocalRef(j_object_id);
}

std::unique_ptr<MinimalVulkanTriangle> g_runtime;

} // namespace ave::android

using namespace ave::android;

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeCreate(JNIEnv* env, jclass, jobject asset_manager, jstring project_path, jobject application_context)
{
    if (g_application_context != nullptr) {
        env->DeleteGlobalRef(g_application_context);
        g_application_context = nullptr;
    }
    if (application_context != nullptr) {
        g_application_context = env->NewGlobalRef(application_context);
    }
    g_runtime = std::make_unique<ave::android::MinimalVulkanTriangle>();
    g_runtime->create(AAssetManager_fromJava(env, asset_manager), ReadJString(env, project_path), g_vm, g_application_context);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeDestroy(JNIEnv* env, jclass)
{
    if (g_runtime) {
        g_runtime->destroy();
        g_runtime.reset();
    }
    if (g_application_context != nullptr) {
        env->DeleteGlobalRef(g_application_context);
        g_application_context = nullptr;
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ave_engine_AveActivity_nativeTouchEvent(
    JNIEnv*, jclass, jfloat x, jfloat y, jint action, jint input_width, jint input_height, jint input_rotation)
{
    if (g_runtime) {
        return g_runtime->onTouchEvent(x, y, action, input_width, input_height, input_rotation) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectPosition(JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z)
{
    if (g_runtime) {
        g_runtime->setObjectPosition(ReadJString(env, object_id), x, y, z);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectRotation(JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z)
{
    if (g_runtime) {
        g_runtime->setObjectRotation(ReadJString(env, object_id), x, y, z);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectScale(JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z)
{
    if (g_runtime) {
        g_runtime->setObjectScale(ReadJString(env, object_id), x, y, z);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectVisible(JNIEnv* env, jclass, jstring object_id, jboolean visible)
{
    if (g_runtime) {
        g_runtime->setObjectVisible(ReadJString(env, object_id), visible == JNI_TRUE);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectColor(
    JNIEnv* env, jclass, jstring object_id, jfloat r, jfloat g, jfloat b, jfloat a)
{
    if (g_runtime) {
        g_runtime->setObjectColor(ReadJString(env, object_id), r, g, b, a);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectTexture(JNIEnv* env, jclass, jstring object_id, jstring texture)
{
    if (g_runtime) {
        g_runtime->setObjectTexture(ReadJString(env, object_id), ReadJString(env, texture));
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectText(JNIEnv* env, jclass, jstring object_id, jstring text)
{
    if (g_runtime) {
        g_runtime->setObjectText(ReadJString(env, object_id), ReadJString(env, text));
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeSetObjectProgress(JNIEnv* env, jclass, jstring object_id, jfloat value)
{
    if (g_runtime) {
        g_runtime->setObjectProgress(ReadJString(env, object_id), value);
    }
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ave_engine_AveActivity_nativeGetObjectPosition(JNIEnv* env, jclass, jstring object_id)
{
    glm::vec3 value{0.0f, 0.0f, 0.0f};
    if (g_runtime) {
        g_runtime->getObjectPosition(ReadJString(env, object_id), value);
    }
    return MakeVec3Array(env, value);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ave_engine_AveActivity_nativeGetObjectRotation(JNIEnv* env, jclass, jstring object_id)
{
    glm::vec3 value{0.0f, 0.0f, 0.0f};
    if (g_runtime) {
        g_runtime->getObjectRotation(ReadJString(env, object_id), value);
    }
    return MakeVec3Array(env, value);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ave_engine_AveActivity_nativeGetObjectScale(JNIEnv* env, jclass, jstring object_id)
{
    glm::vec3 value{1.0f, 1.0f, 1.0f};
    if (g_runtime) {
        g_runtime->getObjectScale(ReadJString(env, object_id), value);
    }
    return MakeVec3Array(env, value);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ave_engine_AveActivity_nativeGetObjectVisible(JNIEnv* env, jclass, jstring object_id)
{
    bool value = false;
    if (g_runtime) {
        g_runtime->getObjectVisible(ReadJString(env, object_id), value);
    }
    return value ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ave_engine_AveActivity_nativeGetObjectColor(JNIEnv* env, jclass, jstring object_id)
{
    glm::vec4 value{1.0f, 1.0f, 1.0f, 1.0f};
    if (g_runtime) {
        g_runtime->getObjectColor(ReadJString(env, object_id), value);
    }
    return MakeVec4Array(env, value);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ave_engine_AveActivity_nativeGetObjectTexture(JNIEnv* env, jclass, jstring object_id)
{
    std::string value;
    if (g_runtime) {
        g_runtime->getObjectTexture(ReadJString(env, object_id), value);
    }
    return env->NewStringUTF(value.c_str());
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ave_engine_AveActivity_nativeGetObjectProgress(JNIEnv* env, jclass, jstring object_id)
{
    float value = 0.0f;
    if (g_runtime) {
        g_runtime->getObjectProgress(ReadJString(env, object_id), value);
    }
    return value;
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
Java_com_ave_engine_AveActivity_nativeSetForeground(JNIEnv*, jclass, jboolean foreground)
{
    if (g_runtime) {
        g_runtime->setForeground(foreground == JNI_TRUE);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeResize(JNIEnv*, jclass, jint width, jint height)
{
    if (g_runtime) {
        g_runtime->resize(width, height);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ave_engine_AveActivity_nativeRegisterFontAtlas(JNIEnv* env, jclass, jint width, jint height, jintArray jpixels)
{
    jint* pixels = env->GetIntArrayElements(jpixels, nullptr);
    if (pixels && g_runtime) {
        g_runtime->registerFontAtlas(width, height, pixels);
        env->ReleaseIntArrayElements(jpixels, pixels, JNI_ABORT);
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ave_engine_AveActivity_nativeInstantiatePrefab(
    JNIEnv* env, jclass, jstring prefab_path, jstring parent_id, jfloat x, jfloat y, jfloat z)
{
    if (g_runtime) {
        std::string res = g_runtime->instantiatePrefab(
            ReadJString(env, prefab_path),
            ReadJString(env, parent_id),
            x,
            y,
            z);
        return env->NewStringUTF(res.c_str());
    }
    return env->NewStringUTF("");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ave_engine_AveActivity_nativeDestroyObject(JNIEnv* env, jclass, jstring object_id)
{
    if (g_runtime) {
        return g_runtime->destroyObject(ReadJString(env, object_id)) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}
