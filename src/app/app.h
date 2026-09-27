// Application: owns the data, the results feed, the Vulkan renderer and the
// Skia dashboard, and implements navigation (pan/zoom/orbit, drill-down
// nation -> county -> town -> village).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/app/camera.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/geo/region_tree.h"
#include "src/render/renderer.h"
#include "src/render/vk_context.h"
#include "src/ui/avatars.h"
#include "src/ui/dashboard.h"
#include "src/ui/i18n.h"
#include "src/ui/text.h"

struct GLFWwindow;

namespace twn::app {

struct AppOptions {
  std::string root = ".";  // directory containing data/ and assets/
  std::string results_path;  // results JSON to watch ("" = data/election/2026/results.json)
  bool simulate = false;
  double sim_duration_s = 180;
  double sim_progress = -1;  // >= 0: start the simulation at this progress
  uint64_t seed = 20261128;
  bool headless = false;
  int width = 1600;
  int height = 900;
  std::string screenshot;  // headless: output PNG path
  std::string focus;       // region code to start in (e.g. "63000")
  std::string hover;       // headless: region code to show hovered
  int mode = 0;            // ui::ColorMode
  ui::Lang lang = ui::Lang::kZhTW;  // UI language; Traditional Chinese by default
  bool help = false;
  bool validation = false;
  std::string font_dir = "/usr/share/fonts";
  float yaw_deg = 0;
  float pitch_deg = 38;
};

class App {
 public:
  App();
  ~App();
  int Run(const AppOptions& options);

  // GLFW callbacks.
  void OnMouseButton(int button, int action, int mods);
  void OnCursor(double x, double y);
  void OnScroll(double dx, double dy);
  void OnKey(int key, int action, int mods);
  void OnResize();

 private:
  struct LayerInfo {
    int gpu_layer = -1;
    std::vector<int> regions;  // slot -> region id
    std::vector<render::RegionStyle> styles;
  };

  bool Init(std::string* error);
  void Shutdown();
  void Tick(double now, float dt);
  bool RenderFrame(double now);
  int SaveScreenshot(const std::string& path);

  LayerInfo* LayerForChildren(int parent);
  int DisplayParent() const;  // region whose children are extruded
  void SetFocus(int region, bool animate);
  void UpdateHover();
  int PickRegion(glm::vec2 screen) const;
  float ExtrudeScale() const;
  void BuildFrame(render::FrameInput* in, ui::DashboardModel* model);

  AppOptions opt_;
  GLFWwindow* window_ = nullptr;
  geo::RegionTree tree_;
  election::ElectionData data_;
  std::unique_ptr<election::ResultsSource> source_;
  election::SimulatedResultsSource* sim_ = nullptr;  // owned by source_ when simulating
  std::shared_ptr<const election::ResultsSnapshot> snapshot_;
  std::unique_ptr<election::ResultsView> results_;
  std::string source_error_;

  vk::VkContext ctx_;
  render::Renderer renderer_;
  Camera camera_;
  ui::Fonts fonts_;
  std::unique_ptr<ui::Avatars> avatars_;
  std::unique_ptr<ui::Dashboard> dashboard_;
  std::vector<uint8_t> overlay_;
  uint64_t overlay_version_ = 0;

  std::unordered_map<int, LayerInfo> layers_;  // parent region -> layer of its children
  std::unordered_map<int, float> heights_;     // animated extrusion per region
  std::unordered_map<int, float> highlights_;

  int focus_ = 0;
  int hover_ = -1;
  ui::ColorMode mode_ = ui::ColorMode::kLeader;
  ui::Lang lang_ = ui::Lang::kZhTW;
  bool show_help_ = false;
  glm::vec2 mouse_{0.f};
  glm::vec2 press_pos_{0.f};
  int drag_button_ = -1;
  bool dragged_ = false;
  bool resize_pending_ = false;
  bool keys_[512] = {};
  double fps_ = 0;
};

}  // namespace twn::app
