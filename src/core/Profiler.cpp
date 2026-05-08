#include "ave/core/Profiler.h"

namespace ave::core {

Profiler::Scope::Scope(Profiler& profiler, std::string name)
    : profiler_(profiler)
    , name_(std::move(name))
    , start_(std::chrono::high_resolution_clock::now())
{
}

Profiler::Scope::~Scope()
{
    auto const end = std::chrono::high_resolution_clock::now();
    auto const milliseconds = std::chrono::duration<double, std::milli>(end - start_).count();
    profiler_.AddSample(std::move(name_), milliseconds);
}

void Profiler::BeginFrame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
}

void Profiler::AddSample(std::string name, double milliseconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.push_back(ProfileSample{std::move(name), milliseconds});
}

std::vector<ProfileSample> Profiler::Samples() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

} // namespace ave::core
