#pragma once

#include <cstdint>
#include <string>

namespace ave::rhi {

struct VulkanDeviceConfig {
    bool enable_validation = true;
};

class VulkanDevice {
public:
    bool Initialize(VulkanDeviceConfig const& config);
    void Shutdown();
    bool IsInitialized() const noexcept;
    void SubmitDebugWork(std::string const& label, uint32_t command_buffer_count);

private:
    bool initialized_ = false;
};

} // namespace ave::rhi
