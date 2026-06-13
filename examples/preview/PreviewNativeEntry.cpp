#include "PreviewNativeEntry.h"

#include <iostream>
#include <string>

#if AVE_PREVIEW_HAS_JNI
#include <jni.h>
#ifndef AVE_PREVIEW_JVM_DLL_PATH
#define AVE_PREVIEW_JVM_DLL_PATH "jvm.dll"
#endif
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ave_preview {

class PreviewJavaRuntime::Impl {
public:
    bool Start(std::filesystem::path const& class_dir, PreviewNativeCallbacks* callbacks)
    {
#if AVE_PREVIEW_HAS_JNI
        Stop();
        if (class_dir.empty() || !std::filesystem::exists(class_dir) || callbacks == nullptr) {
            return false;
        }

        callbacks_ = callbacks;
        JNIEnv* env = EnsureJavaEnv(class_dir);
        if (env == nullptr) {
            callbacks_ = nullptr;
            return false;
        }

        jclass bridge_class = env->FindClass("com/ave/engine/PreviewBridge");
        if (CheckJavaException(env) || bridge_class == nullptr) {
            std::cerr << "[preview] failed to find PreviewBridge\n";
            callbacks_ = nullptr;
            return false;
        }
        if (!RegisterPreviewBridgeNatives(env, bridge_class)) {
            env->DeleteLocalRef(bridge_class);
            callbacks_ = nullptr;
            return false;
        }
        env->DeleteLocalRef(bridge_class);

        jclass main_class = env->FindClass("com/ave/preview/PreviewScriptMain");
        if (CheckJavaException(env) || main_class == nullptr) {
            std::cerr << "[preview] failed to find PreviewScriptMain\n";
            callbacks_ = nullptr;
            return false;
        }

        jmethodID ctor = env->GetMethodID(main_class, "<init>", "()V");
        java_handle_method_ = env->GetMethodID(main_class, "handle", "(Ljava/lang/String;)V");
        if (CheckJavaException(env) || ctor == nullptr || java_handle_method_ == nullptr) {
            env->DeleteLocalRef(main_class);
            std::cerr << "[preview] failed to bind PreviewScriptMain methods\n";
            callbacks_ = nullptr;
            return false;
        }

        jobject instance = env->NewObject(main_class, ctor);
        if (CheckJavaException(env) || instance == nullptr) {
            env->DeleteLocalRef(main_class);
            std::cerr << "[preview] failed to create PreviewScriptMain\n";
            callbacks_ = nullptr;
            return false;
        }
        java_script_main_ = env->NewGlobalRef(instance);
        env->DeleteLocalRef(instance);
        env->DeleteLocalRef(main_class);

        active_runtime_ = this;
        active_ = true;
        std::cout << "[preview] Java JNI script runtime started: " << class_dir << "\n";
        return true;
#else
        (void)class_dir;
        (void)callbacks;
        return false;
#endif
    }

    void Stop()
    {
#if AVE_PREVIEW_HAS_JNI
        JNIEnv* env = GetJavaEnv();
        if (env != nullptr && java_script_main_ != nullptr) {
            env->DeleteGlobalRef(java_script_main_);
        }
        java_script_main_ = nullptr;
        java_handle_method_ = nullptr;
        if (active_runtime_ == this) {
            active_runtime_ = nullptr;
        }
        callbacks_ = nullptr;
#endif
        active_ = false;
    }

    bool IsActive() const
    {
        return active_;
    }

    void Send(std::string const& line)
    {
#if AVE_PREVIEW_HAS_JNI
        if (!active_ || java_script_main_ == nullptr || java_handle_method_ == nullptr) {
            return;
        }
        JNIEnv* env = GetJavaEnv();
        if (env == nullptr) {
            return;
        }
        std::string command = line;
        if (!command.empty() && command.back() == '\n') {
            command.pop_back();
        }
        jstring jline = env->NewStringUTF(command.c_str());
        env->CallVoidMethod(java_script_main_, java_handle_method_, jline);
        env->DeleteLocalRef(jline);
        CheckJavaException(env);
#else
        (void)line;
#endif
    }

private:
#if AVE_PREVIEW_HAS_JNI
    JNIEnv* EnsureJavaEnv(std::filesystem::path const& class_dir)
    {
        if (java_vm_ != nullptr) {
            return GetJavaEnv();
        }

        class_path_ = "-Djava.class.path=" + class_dir.string();
        JavaVMOption options[1]{};
        options[0].optionString = class_path_.data();

        JavaVMInitArgs args{};
        args.version = JNI_VERSION_1_8;
        args.nOptions = 1;
        args.options = options;
        args.ignoreUnrecognized = JNI_FALSE;

        auto* create_java_vm = LoadJvmCreateFunction();
        if (create_java_vm == nullptr) {
            return nullptr;
        }

        JNIEnv* env = nullptr;
        jint const result = create_java_vm(&java_vm_, reinterpret_cast<void**>(&env), &args);
        if (result != JNI_OK || env == nullptr) {
            java_vm_ = nullptr;
            std::cerr << "[preview] failed to create JVM for preview scripts\n";
            return nullptr;
        }
        return env;
    }

    using CreateJavaVMFn = jint(JNICALL*)(JavaVM**, void**, void*);

    CreateJavaVMFn LoadJvmCreateFunction()
    {
#if defined(_WIN32)
        if (jvm_module_ == nullptr) {
            jvm_module_ = LoadLibraryA(AVE_PREVIEW_JVM_DLL_PATH);
            if (jvm_module_ == nullptr) {
                jvm_module_ = LoadLibraryA("jvm.dll");
            }
            if (jvm_module_ == nullptr) {
                std::cerr << "[preview] failed to load jvm.dll. Expected: " << AVE_PREVIEW_JVM_DLL_PATH << "\n";
                return nullptr;
            }
        }
        auto* fn = reinterpret_cast<CreateJavaVMFn>(GetProcAddress(jvm_module_, "JNI_CreateJavaVM"));
        if (fn == nullptr) {
            std::cerr << "[preview] failed to resolve JNI_CreateJavaVM\n";
        }
        return fn;
#else
        return nullptr;
#endif
    }

    JNIEnv* GetJavaEnv()
    {
        if (java_vm_ == nullptr) {
            return nullptr;
        }
        JNIEnv* env = nullptr;
        jint const result = java_vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);
        if (result == JNI_OK) {
            return env;
        }
        if (result == JNI_EDETACHED && java_vm_->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) == JNI_OK) {
            return env;
        }
        return nullptr;
    }

    static std::string ReadJavaString(JNIEnv* env, jstring value)
    {
        if (env == nullptr || value == nullptr) {
            return {};
        }
        char const* chars = env->GetStringUTFChars(value, nullptr);
        if (chars == nullptr) {
            return {};
        }
        std::string text(chars);
        env->ReleaseStringUTFChars(value, chars);
        return text;
    }

    static bool CheckJavaException(JNIEnv* env)
    {
        if (env == nullptr || !env->ExceptionCheck()) {
            return false;
        }
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }

    bool RegisterPreviewBridgeNatives(JNIEnv* env, jclass bridge_class)
    {
        JNINativeMethod methods[] = {
            {const_cast<char*>("nativeLog"), const_cast<char*>("(Ljava/lang/String;)V"), reinterpret_cast<void*>(&NativeLog)},
            {const_cast<char*>("nativeSetPosition"), const_cast<char*>("(Ljava/lang/String;FFF)V"), reinterpret_cast<void*>(&NativeSetPosition)},
            {const_cast<char*>("nativeSetRotation"), const_cast<char*>("(Ljava/lang/String;FFF)V"), reinterpret_cast<void*>(&NativeSetRotation)},
            {const_cast<char*>("nativeSetScale"), const_cast<char*>("(Ljava/lang/String;FFF)V"), reinterpret_cast<void*>(&NativeSetScale)},
            {const_cast<char*>("nativeSetVisible"), const_cast<char*>("(Ljava/lang/String;Z)V"), reinterpret_cast<void*>(&NativeSetVisible)},
            {const_cast<char*>("nativeSetColor"), const_cast<char*>("(Ljava/lang/String;FFFF)V"), reinterpret_cast<void*>(&NativeSetColor)},
            {const_cast<char*>("nativeSetTexture"), const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;)V"), reinterpret_cast<void*>(&NativeSetTexture)},
            {const_cast<char*>("nativeSetText"), const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;)V"), reinterpret_cast<void*>(&NativeSetText)},
            {const_cast<char*>("nativeSetProgress"), const_cast<char*>("(Ljava/lang/String;F)V"), reinterpret_cast<void*>(&NativeSetProgress)},
            {const_cast<char*>("nativeDestroyObject"), const_cast<char*>("(Ljava/lang/String;)Z"), reinterpret_cast<void*>(&NativeDestroyObject)},
            {const_cast<char*>("nativeInstantiatePrefab"), const_cast<char*>("(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;FFF)V"), reinterpret_cast<void*>(&NativeInstantiatePrefab)},
        };
        if (env->RegisterNatives(bridge_class, methods, static_cast<jint>(std::size(methods))) != JNI_OK) {
            CheckJavaException(env);
            std::cerr << "[preview] failed to register PreviewBridge JNI methods\n";
            return false;
        }
        return true;
    }

    static PreviewNativeCallbacks* ActiveCallbacks()
    {
        return active_runtime_ != nullptr ? active_runtime_->callbacks_ : nullptr;
    }

    static void JNICALL NativeLog(JNIEnv* env, jclass, jstring message)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaLog(ReadJavaString(env, message));
        }
    }

    static void JNICALL NativeSetPosition(JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaSetPosition(ReadJavaString(env, object_id), x, y, z);
        }
    }

    static void JNICALL NativeSetRotation(JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaSetRotation(ReadJavaString(env, object_id), x, y, z);
        }
    }

    static void JNICALL NativeSetScale(JNIEnv* env, jclass, jstring object_id, jfloat x, jfloat y, jfloat z)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaSetScale(ReadJavaString(env, object_id), x, y, z);
        }
    }

    static void JNICALL NativeSetVisible(JNIEnv* env, jclass, jstring object_id, jboolean visible)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaSetVisible(ReadJavaString(env, object_id), visible == JNI_TRUE);
        }
    }

    static void JNICALL NativeSetColor(JNIEnv* env, jclass, jstring object_id, jfloat r, jfloat g, jfloat b, jfloat a)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaSetColor(ReadJavaString(env, object_id), r, g, b, a);
        }
    }

    static void JNICALL NativeSetTexture(JNIEnv* env, jclass, jstring object_id, jstring texture)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaSetTexture(ReadJavaString(env, object_id), ReadJavaString(env, texture));
        }
    }

    static void JNICALL NativeSetText(JNIEnv* env, jclass, jstring object_id, jstring text)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaSetText(ReadJavaString(env, object_id), ReadJavaString(env, text));
        }
    }

    static void JNICALL NativeSetProgress(JNIEnv* env, jclass, jstring object_id, jfloat value)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaSetProgress(ReadJavaString(env, object_id), value);
        }
    }

    static jboolean JNICALL NativeDestroyObject(JNIEnv* env, jclass, jstring object_id)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            return callbacks->OnJavaDestroyObject(ReadJavaString(env, object_id)) ? JNI_TRUE : JNI_FALSE;
        }
        return JNI_FALSE;
    }

    static void JNICALL NativeInstantiatePrefab(JNIEnv* env, jclass, jstring requested_id, jstring prefab_path, jstring parent_id, jfloat x, jfloat y, jfloat z)
    {
        if (auto* callbacks = ActiveCallbacks()) {
            callbacks->OnJavaInstantiatePrefab(ReadJavaString(env, requested_id),
                                               ReadJavaString(env, prefab_path),
                                               ReadJavaString(env, parent_id),
                                               x,
                                               y,
                                               z);
        }
    }

    JavaVM* java_vm_ = nullptr;
    jobject java_script_main_ = nullptr;
    jmethodID java_handle_method_ = nullptr;
    std::string class_path_;
#if defined(_WIN32)
    HMODULE jvm_module_ = nullptr;
#endif
    inline static Impl* active_runtime_ = nullptr;
#endif

    PreviewNativeCallbacks* callbacks_ = nullptr;
    bool active_ = false;
};

PreviewJavaRuntime::PreviewJavaRuntime()
    : impl_(std::make_unique<Impl>())
{
}

PreviewJavaRuntime::~PreviewJavaRuntime() = default;

bool PreviewJavaRuntime::Start(std::filesystem::path const& class_dir, PreviewNativeCallbacks* callbacks)
{
    return impl_->Start(class_dir, callbacks);
}

void PreviewJavaRuntime::Stop()
{
    impl_->Stop();
}

bool PreviewJavaRuntime::IsActive() const
{
    return impl_->IsActive();
}

void PreviewJavaRuntime::Send(std::string const& line)
{
    impl_->Send(line);
}

} // namespace ave_preview
