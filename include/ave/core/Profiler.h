#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace ave::core {

struct ProfileSample {
    std::string name;
    double milliseconds = 0.0;
};

class Profiler {
public:
    class Scope {
    public:
        Scope(Profiler& profiler, std::string name);
        ~Scope();

    private:
        Profiler& profiler_;
        std::string name_;
        std::chrono::high_resolution_clock::time_point start_;
    };

    void BeginFrame();
    void AddSample(std::string name, double milliseconds);
    std::vector<ProfileSample> Samples() const;

private:
    mutable std::mutex mutex_;
    std::vector<ProfileSample> samples_;
};

} // namespace ave::core
