#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

struct GLFWwindow;
struct ANativeWindow;


namespace vkfw {

struct ContextCreateInfo {
  // Non-owning.
#if defined(__ANDROID__)
  ANativeWindow* window = nullptr;
#else
::GLFWwindow* window = nullptr;
#endif
  bool enable_validation = true;
};

class VkContext {
public:
  VkContext();
  ~VkContext();

  VkContext(VkContext const&) = delete;
  VkContext& operator=(VkContext const&) = delete;

  VkContext(VkContext&&) noexcept;
  VkContext& operator=(VkContext&&) noexcept;

  bool Init(ContextCreateInfo const& info);
  void Shutdown();
  #if defined(__ANDROID__)
  void SetWindow(::ANativeWindow* window);
#else
  void SetWindow(::GLFWwindow* window);
#endif

  bool IsInitialized() const noexcept;

  vk::raii::Context& Context() const;
  vk::raii::Instance& Instance() const;
  vk::raii::SurfaceKHR& Surface() const;
  #if defined(__ANDROID__)
  ::ANativeWindow* Window() const noexcept;
#else
::GLFWwindow* Window() const noexcept;
#endif
  
  vk::raii::PhysicalDevice& PhysicalDevice() const;
  vk::raii::Device& Device() const;
  vk::raii::Queue& GraphicsQueue() const;
  uint32_t GraphicsQueueFamilyIndex() const noexcept;
  bool SupportsDynamicRendering() const noexcept;
  bool UsesCoreDynamicRendering() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace vkfw
