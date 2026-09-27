#include "src/render/renderer.h"

#include <array>
#include <cstring>

#include "src/render/scene_shaders.h"

namespace twn::render {

using namespace ::twn::vk;  // Vulkan entry points (vkCreateBuffer, ...)
namespace {

VkShaderModule MakeModule(VkDevice dev, std::span<const uint32_t> code) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = code.size() * sizeof(uint32_t);
  ci.pCode = code.data();
  VkShaderModule m = VK_NULL_HANDLE;
  vkCreateShaderModule(dev, &ci, nullptr, &m);
  return m;
}

struct PipelineDesc {
  std::span<const uint32_t> vert, frag;
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  bool depth_test = true;
  bool depth_write = true;
  bool blend = true;
  VkCullModeFlags cull = VK_CULL_MODE_NONE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
};

VkPipeline MakePipeline(VkDevice dev, VkRenderPass pass, VkSampleCountFlagBits samples,
                        const PipelineDesc& d) {
  VkShaderModule vs = MakeModule(dev, d.vert);
  VkShaderModule fs = MakeModule(dev, d.frag);
  VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
       vs, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr},
  };
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = static_cast<uint32_t>(d.bindings.size());
  vi.pVertexBindingDescriptions = d.bindings.data();
  vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(d.attributes.size());
  vi.pVertexAttributeDescriptions = d.attributes.data();
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = d.topology;
  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = d.cull;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = samples;
  VkPipelineDepthStencilStateCreateInfo ds{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = d.depth_test;
  ds.depthWriteEnable = d.depth_write;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState att{};
  att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  if (d.blend) {
    // All shaders output premultiplied alpha.
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &att;
  const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dyn;

  VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pci.stageCount = 2;
  pci.pStages = stages;
  pci.pVertexInputState = &vi;
  pci.pInputAssemblyState = &ia;
  pci.pViewportState = &vp;
  pci.pRasterizationState = &rs;
  pci.pMultisampleState = &ms;
  pci.pDepthStencilState = &ds;
  pci.pColorBlendState = &cb;
  pci.pDynamicState = &dy;
  pci.layout = d.layout;
  pci.renderPass = pass;
  VkPipeline pipeline = VK_NULL_HANDLE;
  vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);
  vkDestroyShaderModule(dev, vs, nullptr);
  vkDestroyShaderModule(dev, fs, nullptr);
  return pipeline;
}

struct CubeVertex {
  float x, y, z, nx, ny, nz;
};

std::vector<CubeVertex> MakeCube() {
  // Unit box: xy in [-0.5, 0.5], z in [0, 1]. Bottom face omitted.
  struct Face {
    float n[3];
    float v[4][3];
  };
  const Face faces[] = {
      {{0, 0, 1}, {{-.5f, -.5f, 1}, {.5f, -.5f, 1}, {.5f, .5f, 1}, {-.5f, .5f, 1}}},
      {{1, 0, 0}, {{.5f, -.5f, 0}, {.5f, .5f, 0}, {.5f, .5f, 1}, {.5f, -.5f, 1}}},
      {{-1, 0, 0}, {{-.5f, .5f, 0}, {-.5f, -.5f, 0}, {-.5f, -.5f, 1}, {-.5f, .5f, 1}}},
      {{0, 1, 0}, {{.5f, .5f, 0}, {-.5f, .5f, 0}, {-.5f, .5f, 1}, {.5f, .5f, 1}}},
      {{0, -1, 0}, {{-.5f, -.5f, 0}, {.5f, -.5f, 0}, {.5f, -.5f, 1}, {-.5f, -.5f, 1}}},
  };
  std::vector<CubeVertex> out;
  for (const Face& f : faces) {
    for (int i : {0, 1, 2, 0, 2, 3}) {
      out.push_back({f.v[i][0], f.v[i][1], f.v[i][2], f.n[0], f.n[1], f.n[2]});
    }
  }
  return out;
}

void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                  VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src,
                  VkPipelineStageFlags dst) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.oldLayout = from;
  b.newLayout = to;
  b.srcAccessMask = src_access;
  b.dstAccessMask = dst_access;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
}

}  // namespace

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(vk::VkContext* ctx, std::string* error) {
  ctx_ = ctx;
  VkDevice dev = ctx_->device();

  // Descriptor set layouts.
  VkDescriptorSetLayoutBinding scene_bindings[2] = {
      {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
  };
  VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dl.bindingCount = 2;
  dl.pBindings = scene_bindings;
  vkCreateDescriptorSetLayout(dev, &dl, nullptr, &scene_set_layout_);
  VkDescriptorSetLayoutBinding overlay_binding = {
      0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  dl.bindingCount = 1;
  dl.pBindings = &overlay_binding;
  vkCreateDescriptorSetLayout(dev, &dl, nullptr, &overlay_set_layout_);

  VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount = 1;
  pl.pSetLayouts = &scene_set_layout_;
  pl.pushConstantRangeCount = 1;
  pl.pPushConstantRanges = &push;
  vkCreatePipelineLayout(dev, &pl, nullptr, &scene_layout_);
  pl.pSetLayouts = &overlay_set_layout_;
  pl.pushConstantRangeCount = 0;
  vkCreatePipelineLayout(dev, &pl, nullptr, &overlay_layout_);

  VkDescriptorPoolSize sizes[3] = {
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16},
  };
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  dp.maxSets = 256;
  dp.poolSizeCount = 3;
  dp.pPoolSizes = sizes;
  vkCreateDescriptorPool(dev, &dp, nullptr, &pool_);

  VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = VK_FILTER_NEAREST;
  sci.minFilter = VK_FILTER_NEAREST;
  sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(dev, &sci, nullptr, &sampler_);

  const std::vector<CubeVertex> cube = MakeCube();
  if (!ctx_->CreateStaticBuffer(cube.data(), cube.size() * sizeof(CubeVertex),
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &cube_) ||
      !ctx_->CreateBuffer(sizeof(RegionStyle), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                          &dummy_styles_)) {
    *error = "buffer allocation failed";
    return false;
  }

  for (Frame& f : frames_) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = ctx_->command_pool();
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev, &ai, &f.cmd);
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(dev, &fi, nullptr, &f.fence);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(dev, &si, nullptr, &f.image_available);
    ctx_->CreateBuffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true,
                       &f.uniforms);
    ctx_->CreateBuffer(sizeof(BarInstance) * kMaxBars, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true,
                       &f.bars);

    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &scene_set_layout_;
    vkAllocateDescriptorSets(dev, &dai, &f.base_set);
    dai.pSetLayouts = &overlay_set_layout_;
    vkAllocateDescriptorSets(dev, &dai, &f.overlay_set);

    VkDescriptorBufferInfo ubo{f.uniforms.buffer, 0, sizeof(FrameUniforms)};
    VkDescriptorBufferInfo ssbo{dummy_styles_.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet w[2] = {};
    w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = w[1].dstSet = f.base_set;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &ubo;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &ssbo;
    vkUpdateDescriptorSets(dev, 2, w, 0, nullptr);
  }

  if (!CreateRenderPass(error) || !CreatePipelines(error) || !CreateTargetResources(error)) {
    return false;
  }
  return true;
}

bool Renderer::CreateRenderPass(std::string* error) {
  const bool msaa = ctx_->msaa() != VK_SAMPLE_COUNT_1_BIT;
  const VkImageLayout final_layout = ctx_->headless() ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                      : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  std::vector<VkAttachmentDescription> atts;
  VkAttachmentDescription color{};
  color.format = ctx_->color_format();
  color.samples = ctx_->msaa();
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : final_layout;
  atts.push_back(color);
  VkAttachmentDescription depth = color;
  depth.format = ctx_->depth_format();
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  atts.push_back(depth);
  if (msaa) {
    VkAttachmentDescription resolve = color;
    resolve.samples = VK_SAMPLE_COUNT_1_BIT;
    resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolve.finalLayout = final_layout;
    atts.push_back(resolve);
  }
  VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkAttachmentReference resolve_ref{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &color_ref;
  sub.pDepthStencilAttachment = &depth_ref;
  sub.pResolveAttachments = msaa ? &resolve_ref : nullptr;
  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep.dstStageMask = dep.srcStageMask;
  dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rp.attachmentCount = static_cast<uint32_t>(atts.size());
  rp.pAttachments = atts.data();
  rp.subpassCount = 1;
  rp.pSubpasses = &sub;
  rp.dependencyCount = 1;
  rp.pDependencies = &dep;
  if (vkCreateRenderPass(ctx_->device(), &rp, nullptr, &render_pass_) != VK_SUCCESS) {
    *error = "vkCreateRenderPass failed";
    return false;
  }
  return true;
}

bool Renderer::CreatePipelines(std::string* error) {
  VkDevice dev = ctx_->device();
  const VkSampleCountFlagBits samples = ctx_->msaa();

  PipelineDesc ground;
  ground.vert = shaders::ground_vert();
  ground.frag = shaders::ground_frag();
  ground.blend = false;
  ground.layout = scene_layout_;
  ground_pipeline_ = MakePipeline(dev, render_pass_, samples, ground);

  PipelineDesc map;
  map.vert = shaders::map_vert();
  map.frag = shaders::map_frag();
  map.bindings = {{0, sizeof(geo::MapVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
  map.attributes = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(geo::MapVertex, x)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(geo::MapVertex, nx)},
      {2, 0, VK_FORMAT_R32_UINT, offsetof(geo::MapVertex, slot)},
  };
  map.layout = scene_layout_;
  map_pipeline_ = MakePipeline(dev, render_pass_, samples, map);

  PipelineDesc lines = map;
  lines.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  lines.depth_write = false;
  line_pipeline_ = MakePipeline(dev, render_pass_, samples, lines);

  PipelineDesc bars;
  bars.vert = shaders::bars_vert();
  bars.frag = shaders::bars_frag();
  bars.bindings = {{0, sizeof(CubeVertex), VK_VERTEX_INPUT_RATE_VERTEX},
                   {1, sizeof(BarInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
  bars.attributes = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
      {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(BarInstance, base)},
      {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(BarInstance, color)},
      {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(BarInstance, size)},
  };
  bars.layout = scene_layout_;
  bar_pipeline_ = MakePipeline(dev, render_pass_, samples, bars);

  PipelineDesc overlay;
  overlay.vert = shaders::overlay_vert();
  overlay.frag = shaders::overlay_frag();
  overlay.depth_test = false;
  overlay.depth_write = false;
  overlay.layout = overlay_layout_;
  overlay_pipeline_ = MakePipeline(dev, render_pass_, samples, overlay);

  if (!ground_pipeline_ || !map_pipeline_ || !line_pipeline_ || !bar_pipeline_ ||
      !overlay_pipeline_) {
    *error = "graphics pipeline creation failed";
    return false;
  }
  return true;
}

bool Renderer::CreateTargetResources(std::string* error) {
  const VkExtent2D e = ctx_->extent();
  if (e.width == 0 || e.height == 0) return true;
  const bool msaa = ctx_->msaa() != VK_SAMPLE_COUNT_1_BIT;
  if (msaa && !ctx_->CreateImage(e.width, e.height, ctx_->color_format(),
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                                 ctx_->msaa(), VK_IMAGE_ASPECT_COLOR_BIT, &color_msaa_)) {
    *error = "cannot create MSAA colour buffer";
    return false;
  }
  if (!ctx_->CreateImage(e.width, e.height, ctx_->depth_format(),
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, ctx_->msaa(),
                         VK_IMAGE_ASPECT_DEPTH_BIT, &depth_)) {
    *error = "cannot create depth buffer";
    return false;
  }
  for (VkImageView target : ctx_->target_views()) {
    std::vector<VkImageView> views;
    if (msaa) views = {color_msaa_.view, depth_.view, target};
    else views = {target, depth_.view};
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = render_pass_;
    fci.attachmentCount = static_cast<uint32_t>(views.size());
    fci.pAttachments = views.data();
    fci.width = e.width;
    fci.height = e.height;
    fci.layers = 1;
    VkFramebuffer fb;
    vkCreateFramebuffer(ctx_->device(), &fci, nullptr, &fb);
    framebuffers_.push_back(fb);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore sem;
    vkCreateSemaphore(ctx_->device(), &si, nullptr, &sem);
    render_finished_.push_back(sem);
  }
  const VkDeviceSize overlay_bytes = VkDeviceSize{e.width} * e.height * 4;
  for (Frame& f : frames_) {
    if (!ctx_->CreateBuffer(overlay_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true,
                            &f.overlay_staging) ||
        !ctx_->CreateImage(e.width, e.height, VK_FORMAT_B8G8R8A8_UNORM,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &f.overlay)) {
      *error = "cannot create overlay texture";
      return false;
    }
    std::memset(f.overlay_staging.mapped, 0, overlay_bytes);
    f.overlay_ready = false;
    f.overlay_version = UINT64_MAX;
    VkDescriptorImageInfo ii{sampler_, f.overlay.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = f.overlay_set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(ctx_->device(), 1, &w, 0, nullptr);
  }
  return true;
}

void Renderer::DestroyTargetResources() {
  VkDevice dev = ctx_->device();
  for (VkFramebuffer fb : framebuffers_) vkDestroyFramebuffer(dev, fb, nullptr);
  for (VkSemaphore s : render_finished_) vkDestroySemaphore(dev, s, nullptr);
  framebuffers_.clear();
  render_finished_.clear();
  ctx_->DestroyImage(&color_msaa_);
  ctx_->DestroyImage(&depth_);
  for (Frame& f : frames_) {
    ctx_->DestroyBuffer(&f.overlay_staging);
    ctx_->DestroyImage(&f.overlay);
  }
}

bool Renderer::Resize(uint32_t width, uint32_t height, std::string* error) {
  vkDeviceWaitIdle(ctx_->device());
  DestroyTargetResources();
  if (ctx_->headless()) ctx_->DestroyTarget();
  if (!ctx_->CreateTarget(width, height, error)) return false;
  return CreateTargetResources(error);
}

void Renderer::Shutdown() {
  if (!ctx_ || !ctx_->device()) return;
  VkDevice dev = ctx_->device();
  vkDeviceWaitIdle(dev);
  for (size_t i = 0; i < layers_.size(); ++i) DestroyLayer(static_cast<int>(i));
  layers_.clear();
  DestroyTargetResources();
  for (Frame& f : frames_) {
    ctx_->DestroyBuffer(&f.uniforms);
    ctx_->DestroyBuffer(&f.bars);
    if (f.fence) vkDestroyFence(dev, f.fence, nullptr);
    if (f.image_available) vkDestroySemaphore(dev, f.image_available, nullptr);
    f = Frame{};
  }
  ctx_->DestroyBuffer(&cube_);
  ctx_->DestroyBuffer(&dummy_styles_);
  for (VkPipeline p : {ground_pipeline_, map_pipeline_, line_pipeline_, bar_pipeline_,
                       overlay_pipeline_}) {
    if (p) vkDestroyPipeline(dev, p, nullptr);
  }
  if (sampler_) vkDestroySampler(dev, sampler_, nullptr);
  if (pool_) vkDestroyDescriptorPool(dev, pool_, nullptr);
  if (scene_layout_) vkDestroyPipelineLayout(dev, scene_layout_, nullptr);
  if (overlay_layout_) vkDestroyPipelineLayout(dev, overlay_layout_, nullptr);
  if (scene_set_layout_) vkDestroyDescriptorSetLayout(dev, scene_set_layout_, nullptr);
  if (overlay_set_layout_) vkDestroyDescriptorSetLayout(dev, overlay_set_layout_, nullptr);
  if (render_pass_) vkDestroyRenderPass(dev, render_pass_, nullptr);
  ground_pipeline_ = map_pipeline_ = line_pipeline_ = bar_pipeline_ = overlay_pipeline_ =
      VK_NULL_HANDLE;
  sampler_ = VK_NULL_HANDLE;
  pool_ = VK_NULL_HANDLE;
  scene_layout_ = overlay_layout_ = VK_NULL_HANDLE;
  scene_set_layout_ = overlay_set_layout_ = VK_NULL_HANDLE;
  render_pass_ = VK_NULL_HANDLE;
  ctx_ = nullptr;
}

void Renderer::WriteLayerSet(Layer& layer, int frame) {
  VkDescriptorBufferInfo ubo{frames_[frame].uniforms.buffer, 0, sizeof(FrameUniforms)};
  VkDescriptorBufferInfo ssbo{layer.styles[frame].buffer, 0, VK_WHOLE_SIZE};
  VkWriteDescriptorSet w[2] = {};
  w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w[0].dstSet = w[1].dstSet = layer.sets[frame];
  w[0].dstBinding = 0;
  w[0].descriptorCount = 1;
  w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  w[0].pBufferInfo = &ubo;
  w[1].dstBinding = 1;
  w[1].descriptorCount = 1;
  w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  w[1].pBufferInfo = &ssbo;
  vkUpdateDescriptorSets(ctx_->device(), 2, w, 0, nullptr);
}

int Renderer::CreateLayer(const geo::MapMesh& mesh) {
  Layer layer;
  layer.alive = true;
  layer.slots = static_cast<uint32_t>(mesh.region_ids.size());
  layer.index_count = static_cast<uint32_t>(mesh.indices.size());
  layer.line_vertex_count = static_cast<uint32_t>(mesh.line_vertices.size());
  bool ok = true;
  if (!mesh.vertices.empty()) {
    ok &= ctx_->CreateStaticBuffer(mesh.vertices.data(),
                                   mesh.vertices.size() * sizeof(geo::MapVertex),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &layer.vertices);
    ok &= ctx_->CreateStaticBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t),
                                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &layer.indices);
  }
  if (!mesh.line_vertices.empty()) {
    ok &= ctx_->CreateStaticBuffer(mesh.line_vertices.data(),
                                   mesh.line_vertices.size() * sizeof(geo::MapVertex),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &layer.lines);
  }
  for (int f = 0; f < kFrames; ++f) {
    ok &= ctx_->CreateBuffer(sizeof(RegionStyle) * std::max(1u, layer.slots),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, &layer.styles[f]);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &scene_set_layout_;
    ok &= vkAllocateDescriptorSets(ctx_->device(), &dai, &layer.sets[f]) == VK_SUCCESS;
    if (ok) WriteLayerSet(layer, f);
  }
  if (!ok) return -1;
  for (size_t i = 0; i < layers_.size(); ++i) {
    if (!layers_[i].alive) {
      layers_[i] = std::move(layer);
      return static_cast<int>(i);
    }
  }
  layers_.push_back(std::move(layer));
  return static_cast<int>(layers_.size() - 1);
}

void Renderer::DestroyLayer(int id) {
  if (id < 0 || id >= static_cast<int>(layers_.size()) || !layers_[id].alive) return;
  vkDeviceWaitIdle(ctx_->device());
  Layer& layer = layers_[id];
  ctx_->DestroyBuffer(&layer.vertices);
  ctx_->DestroyBuffer(&layer.indices);
  ctx_->DestroyBuffer(&layer.lines);
  for (int f = 0; f < kFrames; ++f) {
    ctx_->DestroyBuffer(&layer.styles[f]);
    if (layer.sets[f]) vkFreeDescriptorSets(ctx_->device(), pool_, 1, &layer.sets[f]);
  }
  layer = Layer{};
}

void Renderer::Record(Frame& f, uint32_t image_index, const FrameInput& in) {
  VkCommandBuffer cmd = f.cmd;
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);

  const VkExtent2D e = ctx_->extent();
  // Upload the dashboard overlay if this frame's copy is stale.
  if (in.overlay && f.overlay_version != in.overlay_version) {
    std::memcpy(f.overlay_staging.mapped, in.overlay, size_t{e.width} * e.height * 4);
    f.overlay_version = in.overlay_version;
    ImageBarrier(cmd, f.overlay.image,
                 f.overlay_ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                 : VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {e.width, e.height, 1};
    vkCmdCopyBufferToImage(cmd, f.overlay_staging.buffer, f.overlay.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    ImageBarrier(cmd, f.overlay.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    f.overlay_ready = true;
  }

  VkClearValue clears[3];
  clears[0].color = {{in.clear_color.r, in.clear_color.g, in.clear_color.b, in.clear_color.a}};
  clears[1].depthStencil = {1.0f, 0};
  clears[2].color = clears[0].color;
  VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rbi.renderPass = render_pass_;
  rbi.framebuffer = framebuffers_[image_index];
  rbi.renderArea = {{0, 0}, e};
  rbi.clearValueCount = ctx_->msaa() != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
  rbi.pClearValues = clears;
  vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, static_cast<float>(e.width), static_cast<float>(e.height), 0, 1};
  VkRect2D sc{{0, 0}, e};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  vkCmdSetScissor(cmd, 0, 1, &sc);

  // Ground.
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ground_pipeline_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout_, 0, 1, &f.base_set,
                          0, nullptr);
  vkCmdDraw(cmd, 6, 1, 0, 0);

  const int fi = static_cast<int>(&f - frames_);
  // Map layers: filled prisms first, then outlines.
  for (int pass = 0; pass < 2; ++pass) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pass == 0 ? map_pipeline_ : line_pipeline_);
    for (const LayerDraw& d : in.layers) {
      if (d.layer < 0 || d.layer >= static_cast<int>(layers_.size())) continue;
      Layer& layer = layers_[d.layer];
      if (!layer.alive || (pass == 1 && (!d.outlines || !layer.line_vertex_count))) continue;
      if (pass == 0 && !layer.index_count) continue;
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout_, 0, 1,
                              &layer.sets[fi], 0, nullptr);
      const float push[4] = {pass == 0 ? 0.f : 1.f, pass == 0 ? 0.f : 0.03f,
                             pass == 0 ? d.alpha : d.alpha * d.outline_alpha,
                             d.outline_brightness};
      vkCmdPushConstants(cmd, scene_layout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(push), push);
      const VkDeviceSize zero = 0;
      if (pass == 0) {
        vkCmdBindVertexBuffers(cmd, 0, 1, &layer.vertices.buffer, &zero);
        vkCmdBindIndexBuffer(cmd, layer.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, layer.index_count, 1, 0, 0, 0);
      } else {
        vkCmdBindVertexBuffers(cmd, 0, 1, &layer.lines.buffer, &zero);
        vkCmdDraw(cmd, layer.line_vertex_count, 1, 0, 0);
      }
    }
  }

  // Vote bars.
  const uint32_t bar_count = static_cast<uint32_t>(std::min<size_t>(in.bars.size(), kMaxBars));
  if (bar_count) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bar_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scene_layout_, 0, 1,
                            &f.base_set, 0, nullptr);
    VkBuffer bufs[2] = {cube_.buffer, f.bars.buffer};
    VkDeviceSize offs[2] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offs);
    vkCmdDraw(cmd, 30, bar_count, 0, 0);
  }

  // Dashboard overlay.
  if (f.overlay_ready) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlay_layout_, 0, 1,
                            &f.overlay_set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);
}

Renderer::FrameResult Renderer::DrawFrame(const FrameInput& in) {
  VkDevice dev = ctx_->device();
  const VkExtent2D e = ctx_->extent();
  if (e.width == 0 || e.height == 0 || framebuffers_.empty()) return FrameResult::kResize;
  Frame& f = frames_[frame_index_];
  vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX);

  uint32_t image_index = 0;
  if (!ctx_->headless()) {
    VkResult r = vkAcquireNextImageKHR(dev, ctx_->swapchain(), UINT64_MAX, f.image_available,
                                       VK_NULL_HANDLE, &image_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return FrameResult::kResize;
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return FrameResult::kError;
  }
  vkResetFences(dev, 1, &f.fence);

  // Per-frame data.
  std::memcpy(f.uniforms.mapped, &in.uniforms, sizeof(FrameUniforms));
  for (const LayerDraw& d : in.layers) {
    if (d.layer < 0 || d.layer >= static_cast<int>(layers_.size()) || !d.styles) continue;
    Layer& layer = layers_[d.layer];
    const size_t n = std::min<size_t>(d.styles->size(), layer.slots);
    std::memcpy(layer.styles[frame_index_].mapped, d.styles->data(), n * sizeof(RegionStyle));
  }
  if (!in.bars.empty()) {
    std::memcpy(f.bars.mapped, in.bars.data(),
                std::min<size_t>(in.bars.size(), kMaxBars) * sizeof(BarInstance));
  }

  Record(f, image_index, in);

  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  if (!ctx_->headless()) {
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f.image_available;
    si.pWaitDstStageMask = &wait_stage;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &render_finished_[image_index];
  }
  si.commandBufferCount = 1;
  si.pCommandBuffers = &f.cmd;
  if (vkQueueSubmit(ctx_->queue(), 1, &si, f.fence) != VK_SUCCESS) return FrameResult::kError;

  FrameResult result = FrameResult::kOk;
  if (ctx_->headless()) {
    vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX);
  } else {
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_finished_[image_index];
    const VkSwapchainKHR sc = ctx_->swapchain();
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &image_index;
    const VkResult r = vkQueuePresentKHR(ctx_->queue(), &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) result = FrameResult::kResize;
    else if (r != VK_SUCCESS) result = FrameResult::kError;
  }
  frame_index_ = (frame_index_ + 1) % kFrames;
  return result;
}

bool Renderer::ReadPixels(std::vector<uint8_t>* rgba) {
  if (!ctx_->headless()) return false;
  const VkExtent2D e = ctx_->extent();
  const VkDeviceSize bytes = VkDeviceSize{e.width} * e.height * 4;
  vk::GpuBuffer readback;
  if (!ctx_->CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &readback)) return false;
  VkCommandBuffer cmd = ctx_->BeginOneShot();
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {e.width, e.height, 1};
  vkCmdCopyImageToBuffer(cmd, ctx_->offscreen().image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback.buffer, 1, &copy);
  ctx_->EndOneShot(cmd);
  rgba->resize(bytes);
  std::memcpy(rgba->data(), readback.mapped, bytes);
  ctx_->DestroyBuffer(&readback);
  return true;
}

}  // namespace twn::render
