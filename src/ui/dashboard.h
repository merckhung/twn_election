// The 2D dashboard, rendered with Skia (CPU raster) into an RGBA buffer that
// the Vulkan renderer composites over the 3D map.
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/geo/region_tree.h"
#include "src/ui/avatars.h"
#include "src/ui/i18n.h"
#include "src/ui/text.h"

namespace twn::ui {

enum class ColorMode { kLeader = 0, kProgress, kTurnout, kReferendum, kCount };
const char* ColorModeName(Lang lang, ColorMode m);

struct MapLabel {
  float x = 0, y = 0;  // screen position (px)
  int region = -1;
  float priority = 0;  // larger wins when labels overlap
};

// Screen-space quad around an outlying-island inset (Kinmen, Matsu).
struct InsetFrame {
  int region = -1;
  float x[4] = {}, y[4] = {};
};

struct DashboardModel {
  int width = 0, height = 0;
  const election::ElectionData* data = nullptr;
  const geo::RegionTree* tree = nullptr;
  election::ResultsView* results = nullptr;
  int focus = 0;   // region id currently drilled into
  int hover = -1;  // region id under the cursor
  float mouse_x = 0, mouse_y = 0;
  std::vector<MapLabel> labels;
  std::vector<InsetFrame> insets;
  std::string source;  // results source description
  std::string source_error;
  ColorMode mode = ColorMode::kLeader;
  Lang lang = Lang::kZhTW;
  bool show_help = false;
  double fps = 0;
  std::string gpu;
  std::chrono::system_clock::time_point now;
};

// Screen-space rectangles the app must not treat as map (mouse capture).
struct DashboardLayout {
  float panel_x = 0;  // right panel left edge (px)
  float scale = 1;
};

class Dashboard {
 public:
  Dashboard(Fonts* fonts, Avatars* avatars) : fonts_(fonts), avatars_(avatars) {}

  // Renders into `pixels` (N32 premultiplied, width*height*4 bytes).
  void Render(const DashboardModel& m, uint8_t* pixels);

  static DashboardLayout Layout(int width, int height);

  // Map colour for a region under the given mode (ARGB), used by the 3D view
  // too so the legend and the map always agree.
  static uint32_t RegionColor(const DashboardModel& m, int region, float* strength);

 private:
  void DrawHeader(SkCanvas* c, const DashboardModel& m);
  void DrawLeftColumn(SkCanvas* c, const DashboardModel& m);
  void DrawNationPanel(SkCanvas* c, const DashboardModel& m, SkRect panel);
  void DrawRacePanel(SkCanvas* c, const DashboardModel& m, SkRect panel);
  void DrawLabels(SkCanvas* c, const DashboardModel& m);
  void DrawInsets(SkCanvas* c, const DashboardModel& m);
  void DrawTooltip(SkCanvas* c, const DashboardModel& m);
  void DrawFooter(SkCanvas* c, const DashboardModel& m);
  void DrawHelp(SkCanvas* c, const DashboardModel& m);

  Fonts* fonts_;
  Avatars* avatars_;
  Localizer L_;
  float s_ = 1;  // UI scale
};

// ARGB helpers.
uint32_t MixColor(uint32_t a, uint32_t b, float t);
float Luminance(uint32_t argb);

}  // namespace twn::ui
