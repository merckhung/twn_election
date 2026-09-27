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
#include "src/election/events.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/geo/region_tree.h"
#include "src/news/mock_news.h"
#include "src/news/service.h"
#include "src/render/renderer.h"
#include "src/render/vk_context.h"
#include "src/store/db.h"
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
  double sim_speed = 64;      // simulated seconds per real second (x1 .. x4096)
  double sim_clock = -1;      // >= 0: start at this many minutes after 16:00
  double sim_progress = -1;   // >= 0: start at this fraction of the evening
  uint64_t seed = 20261128;
  bool headless = false;
  int width = 1600;
  int height = 900;
  std::string screenshot;  // headless: output PNG path
  std::string record_dir;  // headless: write a PNG frame sequence here
  double record_seconds = 10;
  double record_fps = 20;
  std::string db_path;     // SQLite database ("" = <root>/twn_election.db)
  bool news = false;       // run the news pipeline
  std::string news_config; // "" = <root>/data/news/feeds.json
  news::LlmConfig llm;
  bool no_llm = false;
  bool mock_news = false;       // generate labelled mock news (on with --simulate --news)
  double mock_news_per_hour = 12;  // per simulated hour (per real minute without --simulate)
  std::string focus;       // region code to start in (e.g. "63000")
  std::string hover;       // headless: region code to show hovered
  int mode = 0;            // ui::ColorMode
  ui::Lang lang = ui::Lang::kZhTW;  // UI language; Traditional Chinese by default
  bool help = false;
  bool show_news = false;  // start with the news panel open
  int chart = 0;           // ui::ChartKind to start in (0 = map)
  bool pip = true;         // picture-in-picture "latest" window
  double chart_tour = 0;   // headless recording: switch view every N seconds
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
  struct MapEffect {
    enum class Kind { kRing, kBeam };
    Kind kind = Kind::kRing;
    int region = -1;
    uint32_t color = 0xFFFFFFFF;
    float age = 0;
    float delay = 0;
    float life = 1;
    float size = 1;  // km: ring radius / beam height scale
  };

  struct LayerInfo {
    int gpu_layer = -1;
    std::vector<int> regions;  // slot -> region id
    std::vector<render::RegionStyle> styles;
  };

  bool Init(std::string* error);
  void Shutdown();
  void Tick(double now, float dt);
  bool RenderFrame(double now);
  int SaveScreenshot(const std::string& path, bool quiet = false);

  LayerInfo* LayerForChildren(int parent);
  int DisplayParent() const;  // region whose children are extruded
  void SetFocus(int region, bool animate);
  void UpdateHover();
  int PickRegion(glm::vec2 screen) const;
  float ExtrudeScale() const;
  void BuildFrame(render::FrameInput* in, ui::DashboardModel* model);

  // Election night (live.cc).
  void OnResults(double now);
  void RebuildRaceStatus(float dt);
  void SpawnEventEffects(const election::EventBatch& batch);
  void SubmitEventNews(const election::EventBatch& batch);
  void RecordToDatabase(const election::EventBatch& batch);
  void UpdateEffects(float dt);
  void BuildEffects(render::FrameInput* in, ui::DashboardModel* m, float D);
  void RefreshNews(double now);
  void GenerateMockNews(double now);
  void SampleHistory(const election::ResultsView& view, double minute);
  void SetChart(ui::ChartKind kind);
  void CycleChartRace(int step);
  bool HandleOverlayClick();
  void TogglePin();
  void SeekSimulation(double minutes);
  std::vector<int> VisibleRegions() const;
  bool ProjectRegion(int region, float* x, float* y) const;

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
  float frame_dt_ = 0;

  election::EventTracker tracker_{&tree_};
  std::unordered_map<std::string, ui::RaceStatus> race_status_;
  std::vector<MapEffect> effects_;
  std::unordered_map<int, float> pulses_;
  std::unordered_map<int, glm::vec3> shown_color_;
  std::unordered_map<int64_t, float> bar_anim_;
  std::string last_recorded_time_;

  store::Database db_;  // UI-thread connection
  std::unique_ptr<news::NewsService> news_;
  ui::NewsView news_view_;
  double next_news_refresh_ = 0;
  bool show_news_ = false;
  int pinned_ = -1;
  ui::ChartKind chart_ = ui::ChartKind::kMap;
  const election::Race* chart_race_ = nullptr;
  bool pip_ = true;
  ui::ChartHistory history_;
  std::unique_ptr<news::MockNewsGenerator> mock_news_;
  double mock_minute_ = -180;  // generated up to (minutes after 16:00)
  int64_t last_news_id_ = -1;
};

}  // namespace twn::app
