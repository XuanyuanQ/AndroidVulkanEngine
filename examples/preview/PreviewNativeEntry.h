#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace ave_preview {

class PreviewNativeCallbacks {
public:
    virtual ~PreviewNativeCallbacks() = default;

    virtual void OnJavaLog(std::string const& message) = 0;
    virtual void OnJavaSetPosition(std::string const& object_id, float x, float y, float z) = 0;
    virtual void OnJavaSetRotation(std::string const& object_id, float x, float y, float z) = 0;
    virtual void OnJavaSetScale(std::string const& object_id, float x, float y, float z) = 0;
    virtual void OnJavaSetVisible(std::string const& object_id, bool visible) = 0;
    virtual void OnJavaSetColor(std::string const& object_id, float r, float g, float b, float a) = 0;
    virtual void OnJavaSetTexture(std::string const& object_id, std::string const& texture) = 0;
    virtual void OnJavaSetText(std::string const& object_id, std::string const& text) = 0;
    virtual void OnJavaSetProgress(std::string const& object_id, float value) = 0;
    virtual bool OnJavaDestroyObject(std::string const& object_id) = 0;
    virtual void OnJavaInstantiatePrefab(std::string const& requested_id,
                                         std::string const& prefab_path,
                                         std::string const& parent_id,
                                         float x,
                                         float y,
                                         float z) = 0;
};

class PreviewJavaRuntime {
public:
    PreviewJavaRuntime();
    ~PreviewJavaRuntime();

    PreviewJavaRuntime(PreviewJavaRuntime const&) = delete;
    PreviewJavaRuntime& operator=(PreviewJavaRuntime const&) = delete;

    bool Start(std::filesystem::path const& class_dir, PreviewNativeCallbacks* callbacks);
    void Stop();
    bool IsActive() const;
    void Send(std::string const& line);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ave_preview
