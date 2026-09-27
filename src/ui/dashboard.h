// The 2D dashboard, rendered with Skia (CPU raster) into an RGBA buffer that
// the Vulkan renderer composites over the 3D map.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "include/core/SkCanvas.h"
#include "src/election/events.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/geo/region_tree.h"
#include "src/store/db.h"
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

// A ring/ripple on the map, already projected to screen space by the app.
struct RingEffect {
  std::vector<SkPoint> points;  // closed polyline
  uint32_t color = 0xFFFFFFFF;
  float alpha = 1;
  float width = 2;
};

// Campaign / projection state of a race, shown as chips and stamps.
struct RaceStatus {
  int declared = -1;  // candidate who declared victory
  int conceded = -1;  // candidate who conceded
  int called = -1;    // projected winner (當選確定)
  float called_age = 1e9f;  // seconds since the projection (stamp animation)
};

// News sentiment read from the SQLite store by the app.
struct NewsView {
  bool enabled = false;
  std::string status;  // classifier / fetch status
  store::SentimentCount total;
  std::unordered_map<std::string, store::SentimentCount> by_candidate;
  std::vector<store::ClassifiedArticle> latest;
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

  // Live election-night state.
  float dt = 0;                   // seconds since the previous frame
  std::vector<RingEffect> rings;  // map ripples (lead changes, projections)
  // Projects a region's label point to the screen; false if off-screen.
  std::function<bool(int region, float* x, float* y)> project;
  std::vector<int> visible_regions;  // regions extruded in the current view
  double clock_minutes = -1;         // snapshot time, minutes after 16:00
  bool simulated = false;
  double sim_speed = 0;
  bool sim_paused = false;
  const std::unordered_map<std::string, RaceStatus>* race_status = nullptr;

  const NewsView* news = nullptr;
  bool show_news = false;  // left column shows the news panel
  int pinned = -1;         // pinned home region
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

  // Feeds new events/deltas from a results update (not called for the
  // baseline snapshot). `clock_minutes` is the update's time after 16:00.
  void PushBatch(const election::EventBatch& batch, double clock_minutes, const DashboardModel& m);
  // Clears banners, call-outs, feed and history (after seeking the clock).
  void ResetLive();
  // A newly classified article: call-out on the subject's county + flashes.
  void PushNews(const store::ClassifiedArticle& article, const DashboardModel& m);
  // Pre-fills the inflow chart (votes per 5-minute bucket after 16:00).
  void SeedInflow(std::vector<int64_t> buckets) { inflow_ = std::move(buckets); }
  // Short status message ("Pinned as home") shown for a moment.
  void Notify(std::string text) {
    notice_ = std::move(text);
    notice_age_ = 0;
  }

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

  // Live layer (dashboard_live.cc).
  enum class CalloutKind { kEvent, kLead, kFlip, kNews };
  struct Callout {
    CalloutKind kind = CalloutKind::kLead;
    election::ElectionEvent event;  // race, leader, previous, margin, progress, time
    int region = -1;                // anchor
    store::ClassifiedArticle news;  // kNews
    float age = 0;
    float life = 4.5f;
  };
  struct Toast {
    election::ElectionEvent event;
    float age = 0;
  };
  struct Floater {
    std::string text;
    uint32_t color = 0xFFFFFFFF;
    float x = 0, y = 0, age = 0;
  };
  struct AnimNum {
    double shown = 0;
    double target = 0;
  };
  void UpdateLive(const DashboardModel& m);
  void DrawRings(SkCanvas* c, const DashboardModel& m);
  void DrawCallouts(SkCanvas* c, const DashboardModel& m);
  void DrawToasts(SkCanvas* c, const DashboardModel& m);
  void DrawFloaters(SkCanvas* c);
  void DrawTimeline(SkCanvas* c, const DashboardModel& m);
  float DrawFeed(SkCanvas* c, const DashboardModel& m, float x, float y, float w, float max_h);
  void DrawNewsPanel(SkCanvas* c, const DashboardModel& m, float x, float y, float w, float h);
  void DrawNewsCounts(SkCanvas* c, const DashboardModel& m, const std::string& candidate_id,
                      float x, float y);
  void DrawNotice(SkCanvas* c, const DashboardModel& m);
  void DrawStamp(SkCanvas* c, float cx, float cy, float size, float age);
  // Event sentence in the current language ("A overtakes B", ...).
  std::string EventText(const election::ElectionEvent& e, const DashboardModel& m) const;
  std::string LeadText(const election::ElectionEvent& e, const DashboardModel& m) const;
  // Smoothly animated number: returns the displayed value for `key`, which
  // rolls towards `target`. Reports increases through `added`.
  double Roll(const std::string& key, double target, double* added = nullptr);
  float Approach(std::unordered_map<std::string, float>& map, const std::string& key,
                 float target, float rate);
  const RaceStatus* StatusOf(const DashboardModel& m, const election::Race& race) const;

  std::deque<Toast> toasts_;
  std::deque<election::ElectionEvent> pending_toasts_;
  std::deque<election::ElectionEvent> feed_;
  std::vector<Callout> callouts_;
  std::vector<Floater> floaters_;
  std::unordered_map<std::string, AnimNum> nums_;
  std::unordered_map<std::string, float> card_y_;
  std::unordered_map<std::string, float> flash_;
  std::unordered_map<std::string, double> float_pending_;
  std::unordered_map<std::string, float> float_cooldown_;
  std::unordered_map<int, int64_t> pending_delta_;
  std::vector<int64_t> inflow_;  // votes added per 5-minute bucket after 16:00
  std::vector<std::pair<float, election::EventType>> markers_;
  float lead_timer_ = 0;
  float flip_budget_ = 0;
  float news_budget_ = 1;
  float time_ = 0;
  float dt_ = 0;
  std::string notice_;
  float notice_age_ = 1e9f;

  Fonts* fonts_;
  Avatars* avatars_;
  Localizer L_;
  float s_ = 1;  // UI scale
};

// ARGB helpers.
uint32_t MixColor(uint32_t a, uint32_t b, float t);
float Luminance(uint32_t argb);

}  // namespace twn::ui
