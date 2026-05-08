#include "ave/rhi/VulkanDevice.h"

namespace ave::rhi {

bool VulkanDevice::Initialize(VulkanDeviceConfig const&)
{
    initialized_ = true;
    return true;
}

void VulkanDevice::Shutdown()
{
    initialized_ = false;
}

bool VulkanDevice::IsInitialized() const noexcept
{
    return initialized_;
}

void VulkanDevice::SubmitDebugWork(std::string const&, uint32_t)
{
}

} // namespace ave::rhi
