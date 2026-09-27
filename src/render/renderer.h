// Vulkan renderer for the 3D map scene and the Skia dashboard overlay.
//
// Scene = sea/grid ground plane + any number of extruded map "layers"
// (one per zoom level) + instanced 3D vote bars + a full-screen overlay
// texture composited on top (the Skia-rendered dashboard, premultiplied).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "src/geo/mesh_builder.h"
#include "src/render/vk_context.h"

namespace twn::render {

struct FrameUniforms {
  glm::mat4 view_proj{1.f};
  glm::vec4 eye{0.f};        // xyz, w = time
  glm::vec4 light_dir{0.f};  // xyz towards light, w = ambient
  glm::vec4 ground{0.f};     // min.xy, max.xy
  glm::vec4 fog{0.f};        // rgb, density
  glm::vec4 sea{0.f};        // rgb, grid spacing
};

struct RegionStyle {
  glm::vec4 color{0.5f, 0.5f, 0.5f, 1.f};  // rgb, alpha
  glm::vec4 params{0.f};                   // height, highlight, base z, unused
};

struct BarInstance {
  glm::vec4 base{0.f};   // x, y, z0, width x
  glm::vec4 color{1.f};  // rgb, alpha
  glm::vec4 size{0.f};   // height, highlight, width y, unused
};

struct LayerDraw {
  int layer = -1;
  const std::vector<RegionStyle>* styles = nullptr;  // one per mesh slot
  float alpha = 1.f;
  bool outlines = true;
  float outline_alpha = 0.9f;
  float outline_brightness = 0.1f;
};

struct FrameInput {
  FrameUniforms uniforms;
  glm::vec4 clear_color{0.02f, 0.05f, 0.09f, 1.f};
  std::vector<LayerDraw> layers;
  std::vector<BarInstance> bars;
  // Dashboard overlay: Skia N32 premultiplied pixels (BGRA in memory),
  // exactly extent().width * extent().height * 4 bytes.
  const uint8_t* overlay = nullptr;
  uint64_t overlay_version = 0;
};

class Renderer {
 public:
  enum class FrameResult { kOk, kResize, kError };

  ~Renderer();
  bool Init(vk::VkContext* ctx, std::string* error);
  void Shutdown();

  // Uploads a map mesh; returns a layer id.
  int CreateLayer(const geo::MapMesh& mesh);
  void DestroyLayer(int layer);

  FrameResult DrawFrame(const FrameInput& in);
  bool Resize(uint32_t width, uint32_t height, std::string* error);

  // Headless mode: copies the last rendered frame as tightly packed RGBA8.
  bool ReadPixels(std::vector<uint8_t>* rgba);

  VkExtent2D extent() const { return ctx_->extent(); }

 private:
  static constexpr int kFrames = 2;
  static constexpr uint32_t kMaxBars = 16384;

  struct Layer {
    bool alive = false;
    vk::GpuBuffer vertices, indices, lines;
    uint32_t index_count = 0, line_vertex_count = 0, slots = 0;
    vk::GpuBuffer styles[kFrames];
    VkDescriptorSet sets[kFrames] = {};
  };

  struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    vk::GpuBuffer uniforms;
    vk::GpuBuffer bars;
    vk::GpuBuffer overlay_staging;
    vk::GpuImage overlay;
    bool overlay_ready = false;
    uint64_t overlay_version = UINT64_MAX;
    VkDescriptorSet base_set = VK_NULL_HANDLE;
    VkDescriptorSet overlay_set = VK_NULL_HANDLE;
  };

  bool CreateRenderPass(std::string* error);
  bool CreatePipelines(std::string* error);
  bool CreateTargetResources(std::string* error);
  void DestroyTargetResources();
  void WriteLayerSet(Layer& layer, int frame);
  void Record(Frame& f, uint32_t image_index, const FrameInput& in);

  vk::VkContext* ctx_ = nullptr;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout scene_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout overlay_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout scene_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout overlay_layout_ = VK_NULL_HANDLE;
  VkPipeline ground_pipeline_ = VK_NULL_HANDLE;
  VkPipeline map_pipeline_ = VK_NULL_HANDLE;
  VkPipeline line_pipeline_ = VK_NULL_HANDLE;
  VkPipeline bar_pipeline_ = VK_NULL_HANDLE;
  VkPipeline overlay_pipeline_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  vk::GpuBuffer cube_;
  vk::GpuBuffer dummy_styles_;
  vk::GpuImage color_msaa_;
  vk::GpuImage depth_;
  std::vector<VkFramebuffer> framebuffers_;
  std::vector<VkSemaphore> render_finished_;  // one per target image
  Frame frames_[kFrames];
  int frame_index_ = 0;
  std::vector<Layer> layers_;
};

}  // namespace twn::render
