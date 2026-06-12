#include "ave/resource/AssetManager.h"

#include <utility>

namespace ave::resource {

void AssetManager::Initialize(core::JobSystem& jobs)
{
    jobs_ = &jobs;
}

std::future<AssetHandle> AssetManager::LoadAsync(std::string path)
{
    if (jobs_ == nullptr) {
        std::promise<AssetHandle> promise;
        AssetHandle handle{next_asset_id_++};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.push_back(PendingAsset{handle, std::move(path)});
        }
        promise.set_value(handle);
        return promise.get_future();
    }

    return jobs_->Enqueue([this, path = std::move(path)]() mutable {
        AssetHandle handle{next_asset_id_++};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.push_back(PendingAsset{handle, std::move(path)});
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
