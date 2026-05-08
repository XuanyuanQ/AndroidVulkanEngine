#include "ave/resource/AssetManager.h"

namespace ave::resource {

void AssetManager::Initialize(core::JobSystem& jobs, GpuUploadQueue& upload_queue)
{
    jobs_ = &jobs;
    upload_queue_ = &upload_queue;
}

std::future<AssetHandle> AssetManager::LoadAsync(std::string path)
{
    return jobs_->Enqueue([this, path = std::move(path)]() mutable {
        AssetHandle handle{};

        {
            std::lock_guard<std::mutex> lock(mutex_);
            handle.id = next_asset_id_++;
            completed_.push_back(PendingAsset{handle, path});
        }

        if (upload_queue_ != nullptr) {
            upload_queue_->Enqueue(UploadRequest{handle.id, path, 0});
        }

        return handle;
    });
}

std::vector<PendingAsset> AssetManager::CompletedAssets() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return completed_;
}

} // namespace ave::resource
