#include "src/render/vk_context.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

// GLFW's Vulkan helpers, declared manually because we build with
// VK_NO_PROTOTYPES and include Vulkan headers ourselves.
extern "C" {
VkResult glfwCreateWindowSurface(VkInstance, GLFWwindow*, const VkAllocationCallbacks*,
                                 VkSurfaceKHR*);
const char** glfwGetRequiredInstanceExtensions(uint32_t* count);
}

namespace twn::vk {
namespace {

bool HasLayer(const char* name) {
  uint32_t n = 0;
  vkEnumerateInstanceLayerProperties(&n, nullptr);
  std::vector<VkLayerProperties> layers(n);
  vkEnumerateInstanceLayerProperties(&n, layers.data());
  for (const auto& l : layers) {
    if (std::strcmp(l.layerName, name) == 0) return true;
  }
  return false;
}

std::string Fail(const char* what, VkResult r) { return std::string(what) + ": " + ResultString(r); }

}  // namespace

VkContext::~VkContext() { Shutdown(); }

bool VkContext::Init(const Options& options, GLFWwindow* window, std::string* error) {
  options_ = options;
  window_ = window;
  if (!LoadVulkanLoader()) {
    *error = "Vulkan loader (libvulkan.so.1) not found";
    return false;
  }

  std::vector<const char*> extensions;
  if (!options.headless) {
    uint32_t count = 0;
    const char** glfw_ext = glfwGetRequiredInstanceExtensions(&count);
    if (!glfw_ext) {
      *error = "GLFW reports Vulkan presentation is unsupported";
      return false;
    }
    extensions.assign(glfw_ext, glfw_ext + count);
  }
  std::vector<const char*> layers;
  if (options.validation) {
    if (HasLayer("VK_LAYER_KHRONOS_validation")) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
    } else {
      std::fprintf(stderr, "warning: VK_LAYER_KHRONOS_validation not installed\n");
    }
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "twn_election";
  app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app.pEngineName = "twn";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  ici.ppEnabledExtensionNames = extensions.data();
  ici.enabledLayerCount = static_cast<uint32_t>(layers.size());
  ici.ppEnabledLayerNames = layers.data();
  if (VkResult r = vkCreateInstance(&ici, nullptr, &instance_); r != VK_SUCCESS) {
    *error = Fail("vkCreateInstance", r);
    return false;
  }
  LoadInstanceFunctions(instance_);

  if (!options.headless) {
    if (VkResult r = glfwCreateWindowSurface(instance_, window_, nullptr, &surface_);
        r != VK_SUCCESS) {
      *error = Fail("glfwCreateWindowSurface", r);
      return false;
    }
  }
  if (!PickDevice(error)) return false;

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queue_family_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;
  std::vector<const char*> dev_ext;
  if (!options.headless) dev_ext.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  VkPhysicalDeviceFeatures features{};
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = static_cast<uint32_t>(dev_ext.size());
  dci.ppEnabledExtensionNames = dev_ext.data();
  dci.pEnabledFeatures = &features;
  if (VkResult r = vkCreateDevice(physical_, &dci, nullptr, &device_); r != VK_SUCCESS) {
    *error = Fail("vkCreateDevice", r);
    return false;
  }
  LoadDeviceFunctions(device_);
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = queue_family_;
  vkCreateCommandPool(device_, &pci, nullptr, &pool_);

  // Depth format.
  for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(physical_, f, &fp);
    if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      depth_format_ = f;
      break;
    }
  }
  return CreateTarget(options.width, options.height, error);
}

bool VkContext::PickDevice(std::string* error) {
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(instance_, &n, nullptr);
  if (n == 0) {
    *error = "no Vulkan physical devices";
    return false;
  }
  std::vector<VkPhysicalDevice> devices(n);
  vkEnumeratePhysicalDevices(instance_, &n, devices.data());

  int best_score = -1;
  for (VkPhysicalDevice pd : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf.data());
    for (uint32_t i = 0; i < qn; ++i) {
      if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
      if (surface_) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
        if (!present) continue;
      }
      int score = 1;
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 4;
      else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 3;
      else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score = 2;
      if (score > best_score) {
        best_score = score;
        physical_ = pd;
        queue_family_ = i;
        device_name_ = props.deviceName;
        const VkSampleCountFlags counts =
            props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
        msaa_ = VK_SAMPLE_COUNT_1_BIT;
        for (VkSampleCountFlagBits s : {VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_4_BIT,
                                        VK_SAMPLE_COUNT_2_BIT}) {
          if (static_cast<int>(s) <= options_.msaa && (counts & s)) {
            msaa_ = s;
            break;
          }
        }
      }
      break;
    }
  }
  if (!physical_) {
    *error = "no Vulkan device with a graphics(+present) queue";
    return false;
  }
  vkGetPhysicalDeviceMemoryProperties(physical_, &mem_props_);
  return true;
}

bool VkContext::CreateTarget(uint32_t width, uint32_t height, std::string* error) {
  width = std::max(1u, width);
  height = std::max(1u, height);
  if (options_.headless) {
    color_format_ = VK_FORMAT_R8G8B8A8_UNORM;
    extent_ = {width, height};
    if (!CreateImage(width, height, color_format_,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &offscreen_)) {
      *error = "cannot create offscreen target";
      return false;
    }
    target_images_ = {offscreen_.image};
    target_views_ = {offscreen_.view};
    return true;
  }

  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
  uint32_t fn = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fn, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(fn);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fn, formats.data());
  VkSurfaceFormatKHR chosen = formats[0];
  for (const auto& f : formats) {
    if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = f;
      break;
    }
  }
  color_format_ = chosen.format;

  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  if (!options_.vsync) {
    uint32_t pn = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &pn, nullptr);
    std::vector<VkPresentModeKHR> modes(pn);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &pn, modes.data());
    for (auto m : modes) {
      if (m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;
    }
  }
  if (caps.currentExtent.width != 0xFFFFFFFFu) {
    extent_ = caps.currentExtent;
  } else {
    extent_ = {std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
               std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)};
  }
  if (extent_.width == 0 || extent_.height == 0) return true;  // minimised

  uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) image_count = std::min(image_count, caps.maxImageCount);
  VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = surface_;
  sci.minImageCount = image_count;
  sci.imageFormat = chosen.format;
  sci.imageColorSpace = chosen.colorSpace;
  sci.imageExtent = extent_;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = mode;
  sci.clipped = VK_TRUE;
  sci.oldSwapchain = swapchain_;
  VkSwapchainKHR new_swapchain;
  if (VkResult r = vkCreateSwapchainKHR(device_, &sci, nullptr, &new_swapchain); r != VK_SUCCESS) {
    *error = Fail("vkCreateSwapchainKHR", r);
    return false;
  }
  DestroyTarget();
  swapchain_ = new_swapchain;
  uint32_t n = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
  target_images_.resize(n);
  vkGetSwapchainImagesKHR(device_, swapchain_, &n, target_images_.data());
  target_views_.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = target_images_[i];
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = color_format_;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device_, &vci, nullptr, &target_views_[i]);
  }
  return true;
}

void VkContext::DestroyTarget() {
  if (!device_) return;
  if (options_.headless) {
    DestroyImage(&offscreen_);
  } else {
    for (VkImageView v : target_views_) vkDestroyImageView(device_, v, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
  target_views_.clear();
  target_images_.clear();
}

void VkContext::Shutdown() {
  if (device_) {
    vkDeviceWaitIdle(device_);
    DestroyTarget();
    if (pool_) vkDestroyCommandPool(device_, pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (surface_) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
  if (instance_) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

uint32_t VkContext::FindMemoryType(uint32_t bits, VkMemoryPropertyFlags props) const {
  for (uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) && (mem_props_.memoryTypes[i].propertyFlags & props) == props) return i;
  }
  return UINT32_MAX;
}

bool VkContext::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host_visible,
                             GpuBuffer* out) {
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = std::max<VkDeviceSize>(size, 16);
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device_, &bci, nullptr, &out->buffer) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(device_, out->buffer, &req);
  const VkMemoryPropertyFlags props =
      host_visible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                   : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
  if (mai.memoryTypeIndex == UINT32_MAX) return false;
  if (vkAllocateMemory(device_, &mai, nullptr, &out->memory) != VK_SUCCESS) return false;
  vkBindBufferMemory(device_, out->buffer, out->memory, 0);
  out->size = bci.size;
  if (host_visible) vkMapMemory(device_, out->memory, 0, VK_WHOLE_SIZE, 0, &out->mapped);
  return true;
}

void VkContext::DestroyBuffer(GpuBuffer* b) {
  if (b->buffer) vkDestroyBuffer(device_, b->buffer, nullptr);
  if (b->memory) vkFreeMemory(device_, b->memory, nullptr);
  *b = GpuBuffer{};
}

bool VkContext::CreateStaticBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                   GpuBuffer* out) {
  GpuBuffer staging;
  if (!CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, &staging)) return false;
  std::memcpy(staging.mapped, data, size);
  if (!CreateBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, out)) {
    DestroyBuffer(&staging);
    return false;
  }
  VkCommandBuffer cmd = BeginOneShot();
  VkBufferCopy region{0, 0, size};
  vkCmdCopyBuffer(cmd, staging.buffer, out->buffer, 1, &region);
  EndOneShot(cmd);
  DestroyBuffer(&staging);
  return true;
}

bool VkContext::CreateImage(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage,
                            VkSampleCountFlagBits samples, VkImageAspectFlags aspect,
                            GpuImage* out) {
  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = format;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = samples;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device_, &ici, nullptr, &out->image) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(device_, out->image, &req);
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (mai.memoryTypeIndex == UINT32_MAX) {
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, 0);
  }
  if (vkAllocateMemory(device_, &mai, nullptr, &out->memory) != VK_SUCCESS) return false;
  vkBindImageMemory(device_, out->image, out->memory, 0);
  VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = out->image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = format;
  vci.subresourceRange = {aspect, 0, 1, 0, 1};
  if (vkCreateImageView(device_, &vci, nullptr, &out->view) != VK_SUCCESS) return false;
  out->format = format;
  out->width = w;
  out->height = h;
  return true;
}

void VkContext::DestroyImage(GpuImage* img) {
  if (img->view) vkDestroyImageView(device_, img->view, nullptr);
  if (img->image) vkDestroyImage(device_, img->image, nullptr);
  if (img->memory) vkFreeMemory(device_, img->memory, nullptr);
  *img = GpuImage{};
}

VkCommandBuffer VkContext::BeginOneShot() {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  vkAllocateCommandBuffers(device_, &ai, &cmd);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  return cmd;
}

void VkContext::EndOneShot(VkCommandBuffer cmd) {
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue_);
  vkFreeCommandBuffers(device_, pool_, 1, &cmd);
}

}  // namespace twn::vk
