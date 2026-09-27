// Vulkan instance/device/queue plus the presentation target: either a GLFW
// window swapchain or an offscreen image (headless mode, used for CI and
// screenshots, works with software rasterisers such as Mesa lavapipe).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/render/vk_loader.h"

struct GLFWwindow;

namespace twn::vk {

struct GpuBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void* mapped = nullptr;  // host-visible buffers stay persistently mapped
};

struct GpuImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0, height = 0;
};

class VkContext {
 public:
  struct Options {
    bool validation = false;
    bool headless = false;
    uint32_t width = 1600;
    uint32_t height = 900;
    bool vsync = true;
    int msaa = 4;  // requested sample count (clamped to device support)
  };

  ~VkContext();

  // `window` must be non-null unless options.headless.
  bool Init(const Options& options, GLFWwindow* window, std::string* error);
  void Shutdown();

  // (Re)creates the swapchain or offscreen target at the given size.
  bool CreateTarget(uint32_t width, uint32_t height, std::string* error);
  void DestroyTarget();

  bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible,
                    GpuBuffer* out);
  void DestroyBuffer(GpuBuffer* b);
  // Creates a device-local buffer filled with `data` via a staging copy.
  bool CreateStaticBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                          GpuBuffer* out);
  bool CreateImage(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage,
                   VkSampleCountFlagBits samples, VkImageAspectFlags aspect, GpuImage* out);
  void DestroyImage(GpuImage* img);

  VkCommandBuffer BeginOneShot();
  void EndOneShot(VkCommandBuffer cmd);

  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physical_device() const { return physical_; }
  VkDevice device() const { return device_; }
  VkQueue queue() const { return queue_; }
  uint32_t queue_family() const { return queue_family_; }
  VkCommandPool command_pool() const { return pool_; }
  bool headless() const { return options_.headless; }
  VkSampleCountFlagBits msaa() const { return msaa_; }
  VkFormat color_format() const { return color_format_; }
  VkFormat depth_format() const { return depth_format_; }
  VkExtent2D extent() const { return extent_; }
  VkSwapchainKHR swapchain() const { return swapchain_; }
  const std::vector<VkImage>& target_images() const { return target_images_; }
  const std::vector<VkImageView>& target_views() const { return target_views_; }
  const GpuImage& offscreen() const { return offscreen_; }
  const std::string& device_name() const { return device_name_; }

 private:
  uint32_t FindMemoryType(uint32_t bits, VkMemoryPropertyFlags props) const;
  bool PickDevice(std::string* error);

  Options options_;
  GLFWwindow* window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties mem_props_{};
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = 0;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkSampleCountFlagBits msaa_ = VK_SAMPLE_COUNT_1_BIT;
  VkFormat color_format_ = VK_FORMAT_B8G8R8A8_UNORM;
  VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;
  VkExtent2D extent_{};
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  std::vector<VkImage> target_images_;
  std::vector<VkImageView> target_views_;
  GpuImage offscreen_;
  std::string device_name_;
};

}  // namespace twn::vk
