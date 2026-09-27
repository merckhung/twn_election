#include "src/app/app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"
#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "src/geo/mesh_builder.h"
#include "src/geo/projection.h"
#include "stb_image_write.h"

namespace twn::app {
namespace {

constexpr uint32_t kNeutral = 0xFF2B3D52;
constexpr float kPi = 3.14159265f;

glm::vec4 ToVec4(uint32_t argb, float alpha = 1.f) {
  return glm::vec4(((argb >> 16) & 0xFF) / 255.f, ((argb >> 8) & 0xFF) / 255.f,
                   (argb & 0xFF) / 255.f, alpha);
}

App* FromWindow(GLFWwindow* w) { return static_cast<App*>(glfwGetWindowUserPointer(w)); }

}  // namespace

App::App() = default;

App::~App() { Shutdown(); }

int App::Run(const AppOptions& options) {
  opt_ = options;
  std::string error;
  if (!Init(&error)) {
    std::fprintf(stderr, "twn_election: %s\n", error.c_str());
    return 1;
  }
  if (opt_.music && !opt_.headless) music_.Start();
  if (opt_.headless && !opt_.record_dir.empty()) {
    // Frame sequence at a fixed rate (simulated time advances with it).
    const int frames = static_cast<int>(opt_.record_seconds * opt_.record_fps);
    const float dt = static_cast<float>(1.0 / opt_.record_fps);
    for (int i = 0; i < frames; ++i) {
      const double now = i * dt;
      if (opt_.chart_tour > 0) {
        // Map -> trend -> seats -> margins -> parties -> grid -> map ...
        const int step = static_cast<int>(now / opt_.chart_tour) % static_cast<int>(ui::ChartKind::kCount);
        if (static_cast<int>(chart_) != step) SetChart(static_cast<ui::ChartKind>(step));
      }
      Tick(now, dt);
      RenderFrame(now);
      char name[64];
      std::snprintf(name, sizeof name, "/frame_%05d.png", i);
      if (SaveScreenshot(opt_.record_dir + name, /*quiet=*/true)) return 1;
    }
    std::printf("wrote %d frames to %s\n", frames, opt_.record_dir.c_str());
    return 0;
  }
  if (opt_.headless) {
    // Render a few frames so animations settle, then save.
    for (int i = 0; i < 3; ++i) {
      Tick(1000.0 + i, 10.f);
      RenderFrame(1000.0 + i);
    }
    const int rc = opt_.screenshot.empty() ? 0 : SaveScreenshot(opt_.screenshot);
    return rc;
  }

  double last = glfwGetTime();
  double fps_acc = 0;
  int fps_frames = 0;
  while (!glfwWindowShouldClose(window_)) {
    glfwPollEvents();
    const double now = glfwGetTime();
    const float dt = static_cast<float>(std::min(0.1, now - last));
    last = now;
    fps_acc += dt;
    if (++fps_frames == 30) {
      fps_ = fps_frames / std::max(1e-6, fps_acc);
      fps_acc = 0;
      fps_frames = 0;
    }
    if (resize_pending_) OnResize();
    Tick(now, dt);
    if (!RenderFrame(now)) resize_pending_ = true;
  }
  return 0;
}

bool App::Init(std::string* error) {
  const std::string map_path = opt_.root + "/data/map/villages-10t.json";
  std::string topo;
  if (!election::ReadFile(map_path, &topo)) {
    *error = "cannot read " + map_path + " (use --root to point at the project directory)";
    return false;
  }
  if (!tree_.LoadTopoJson(topo, error)) return false;
  if (!data_.Load(opt_.root + "/data/election/2026", error)) return false;

  if (opt_.simulate) {
    auto sim = std::make_unique<election::SimulatedResultsSource>(&data_, &tree_, opt_.seed,
                                                                  opt_.sim_speed);
    if (opt_.sim_progress >= 0) sim->SeekProgress(opt_.sim_progress);
    if (opt_.sim_clock >= 0) sim->SeekClock(opt_.sim_clock);
    sim_ = sim.get();
    source_ = std::move(sim);
  } else {
    const std::string path = opt_.results_path.empty()
                                 ? opt_.root + "/data/election/2026/results.json"
                                 : opt_.results_path;
    source_ = std::make_unique<election::FileResultsSource>(&data_, path);
  }
  snapshot_ = std::make_shared<election::ResultsSnapshot>();
  results_ = std::make_unique<election::ResultsView>(&data_, &tree_, snapshot_);

  // SQLite store: settings (pinned home region), results/event history, news.
  const std::string db_path = opt_.db_path.empty() ? opt_.root + "/twn_election.db" : opt_.db_path;
  std::string db_error;
  if (!db_.Open(db_path, &db_error)) {
    std::fprintf(stderr, "warning: database disabled: %s\n", db_error.c_str());
  } else if (opt_.simulate) {
    db_.ClearSimulated();  // each simulation run starts from a clean slate
  }
  if (opt_.news && db_.is_open()) {
    news::NewsConfig nc;
    nc.db_path = db_path;
    nc.llm = opt_.llm;
    nc.use_llm = !opt_.no_llm;
    const std::string cfg =
        opt_.news_config.empty() ? opt_.root + "/data/news/feeds.json" : opt_.news_config;
    std::string news_error;
    if (!news::LoadNewsConfig(cfg, data_, &nc, &news_error)) {
      std::fprintf(stderr, "warning: %s (news feeds disabled)\n", news_error.c_str());
    }
    if (!nc.inbox_dir.empty() && nc.inbox_dir[0] != '/') nc.inbox_dir = opt_.root + "/" + nc.inbox_dir;
    news_ = std::make_unique<news::NewsService>(std::move(nc), news::CandidateRefs(data_));
    news_->Start();
    if (opt_.mock_news || opt_.simulate) {
      mock_news_ = std::make_unique<news::MockNewsGenerator>(&data_, opt_.seed);
    }
  }

  if (!fonts_.Load(opt_.font_dir, error)) return false;
  avatars_ = std::make_unique<ui::Avatars>(&fonts_, opt_.root);
  dashboard_ = std::make_unique<ui::Dashboard>(&fonts_, avatars_.get());

  vk::VkContext::Options vo;
  vo.headless = opt_.headless;
  vo.validation = opt_.validation;
  vo.width = opt_.width;
  vo.height = opt_.height;
  if (!opt_.headless) {
    if (!glfwInit()) {
      *error = "glfwInit failed (no display?) - try --headless";
      return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(opt_.width, opt_.height,
                               (std::string(ui::Tr(opt_.lang, "window.title")) + " · twn_election").c_str(),
                               nullptr, nullptr);
    if (!window_) {
      *error = "cannot create window";
      return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int b, int a, int m) {
      FromWindow(w)->OnMouseButton(b, a, m);
    });
    glfwSetCursorPosCallback(window_,
                             [](GLFWwindow* w, double x, double y) { FromWindow(w)->OnCursor(x, y); });
    glfwSetScrollCallback(window_,
                          [](GLFWwindow* w, double x, double y) { FromWindow(w)->OnScroll(x, y); });
    glfwSetKeyCallback(window_, [](GLFWwindow* w, int k, int, int a, int m) {
      FromWindow(w)->OnKey(k, a, m);
    });
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int, int) {
      FromWindow(w)->resize_pending_ = true;
    });
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(window_, &fw, &fh);
    vo.width = fw;
    vo.height = fh;
  }
  if (!ctx_.Init(vo, window_, error)) return false;
  if (!renderer_.Init(&ctx_, error)) return false;
  const VkExtent2D e = renderer_.extent();
  camera_.SetViewport(static_cast<float>(e.width), static_cast<float>(e.height));
  overlay_.assign(size_t{e.width} * e.height * 4, 0);

  mode_ = static_cast<ui::ColorMode>(
      std::clamp(opt_.mode, 0, static_cast<int>(ui::ColorMode::kCount) - 1));
  show_help_ = opt_.help;
  lang_ = opt_.lang;
  show_news_ = opt_.show_news;
  LayerForChildren(0);

  int start = 0;
  if (db_.is_open()) {
    if (auto home = db_.GetSetting("home_region")) pinned_ = tree_.FindByCode(*home);
    if (pinned_ >= 0) start = pinned_;
  }
  if (!opt_.focus.empty()) {
    start = tree_.FindByCode(opt_.focus);
    if (start < 0) {
      *error = "unknown region code for --focus: " + opt_.focus;
      return false;
    }
  }
  SetFocus(start, /*animate=*/false);
  if (sim_ && sim_->clock_minutes() > 0) {
    dashboard_->SeedInflow(sim_->InflowHistory(5, sim_->clock_minutes()));
    // Back-fill the trend history in 5-minute steps up to the start time.
    for (double minute = 10; minute < sim_->clock_minutes(); minute += 5) {
      election::ResultsView view(&data_, &tree_, sim_->SnapshotAtClock(minute));
      SampleHistory(view, minute);
    }
  }
  pip_ = opt_.pip;
  SetChart(static_cast<ui::ChartKind>(
      std::clamp(opt_.chart, 0, static_cast<int>(ui::ChartKind::kCount) - 1)));
  if (!opt_.hover.empty()) hover_ = tree_.FindByCode(opt_.hover);

  std::printf(
      "twn_election: %d regions, %zu races, GPU: %s, MSAA x%d, font: %s, lang: %s, results: %s\n",
      tree_.size(), data_.races().size(), ctx_.device_name().c_str(),
      static_cast<int>(ctx_.msaa()), fonts_.family().c_str(), ui::LangCode(lang_),
      source_->Describe().c_str());
  std::fflush(stdout);
  return true;
}

void App::Shutdown() {
  music_.Stop();
  if (news_) news_->Stop();
  news_.reset();
  if (ctx_.device()) {
    for (auto& [parent, layer] : layers_) renderer_.DestroyLayer(layer.gpu_layer);
    layers_.clear();
    renderer_.Shutdown();
    ctx_.Shutdown();
  }
  if (window_) {
    glfwDestroyWindow(window_);
    glfwTerminate();
    window_ = nullptr;
  }
}

App::LayerInfo* App::LayerForChildren(int parent) {
  auto it = layers_.find(parent);
  if (it != layers_.end()) return &it->second;
  const geo::Region& r = tree_.region(parent);
  if (r.children.empty()) return nullptr;
  const geo::MapMesh mesh = geo::BuildMapMesh(tree_, r.children);
  LayerInfo info;
  info.gpu_layer = renderer_.CreateLayer(mesh);
  info.regions = mesh.region_ids;
  info.styles.resize(info.regions.size());
  return &layers_.emplace(parent, std::move(info)).first->second;
}

int App::DisplayParent() const {
  const geo::Region& f = tree_.region(focus_);
  return f.children.empty() && f.parent >= 0 ? f.parent : focus_;
}

float App::ExtrudeScale() const {
  const geo::Region& r = tree_.region(DisplayParent());
  return std::max(1.f, glm::length(r.size()));
}

void App::SetFocus(int region, bool animate) {
  focus_ = region;
  const geo::Region& r = tree_.region(region);
  LayerForChildren(DisplayParent());
  glm::vec2 mn = r.min, mx = r.max;
  if (r.children.empty()) {
    // Leaf (village): keep neighbours in view.
    const glm::vec2 c = r.center();
    const glm::vec2 half = glm::max(r.size(), glm::vec2(0.6f)) * 1.8f;
    mn = c - half;
    mx = c + half;
  }
  const ui::DashboardLayout layout =
      ui::Dashboard::Layout(renderer_.extent().width, renderer_.extent().height);
  const float right_margin = renderer_.extent().width - layout.panel_x;
  const float pitch = !animate ? opt_.pitch_deg * kPi / 180.f : camera_.pose().pitch;
  const float yaw = !animate ? opt_.yaw_deg * kPi / 180.f : camera_.pose().yaw;
  // Pitch shouldn't be too flat when framing; the map is best read from above.
  const float left_margin = 350 * layout.scale;
  const CameraPose target =
      camera_.Frame(mn, mx, std::clamp(pitch, 0.55f, 1.35f), yaw, left_margin, right_margin);
  if (animate) camera_.FlyTo(target, 1.1f);
  else camera_.SetPose(target);
}

int App::PickRegion(glm::vec2 screen) const {
  if (screen.x >= ui::Dashboard::Layout(renderer_.extent().width, renderer_.extent().height).panel_x) {
    return -1;
  }
  const int dp = DisplayParent();
  const geo::Region& parent = tree_.region(dp);
  float avg = 0;
  int n = 0;
  for (int c : parent.children) {
    if (auto it = heights_.find(c); it != heights_.end()) {
      avg += it->second;
      ++n;
    }
  }
  if (n) avg /= n;
  for (float z : {avg, avg * 0.5f, 0.f}) {
    glm::vec2 p;
    if (!camera_.ScreenToGround(screen, z, &p)) continue;
    const int hit = tree_.ChildAt(dp, p);
    if (hit >= 0) return hit;
  }
  // Context layers: allow jumping to a sibling area (ground level).
  glm::vec2 p;
  if (!camera_.ScreenToGround(screen, 0, &p)) return -1;
  for (int a = tree_.region(dp).parent; a >= 0; a = tree_.region(a).parent) {
    const int hit = tree_.ChildAt(a, p);
    if (hit >= 0) return hit;
  }
  return -1;
}

void App::UpdateHover() {
  if (opt_.headless) return;
  if (chart_ != ui::ChartKind::kMap || dashboard_->Captures(mouse_.x, mouse_.y)) {
    hover_ = -1;
    return;
  }
  hover_ = drag_button_ >= 0 && dragged_ ? hover_ : PickRegion(mouse_);
}

void App::Tick(double now, float dt) {
  frame_dt_ = dt;
  if (auto snap = source_->Poll(now)) {
    snapshot_ = snap;
    results_ = std::make_unique<election::ResultsView>(&data_, &tree_, snapshot_);
    OnResults(now);
  }
  UpdateEffects(dt);
  GenerateMockNews(now);
  RefreshNews(now);
  if (auto* file = dynamic_cast<election::FileResultsSource*>(source_.get())) {
    source_error_ = file->last_error();
  }

  // Keyboard navigation.
  if (!opt_.headless && chart_ == ui::ChartKind::kMap) {
    glm::vec2 pan(0.f);
    if (keys_[GLFW_KEY_A] || keys_[GLFW_KEY_LEFT]) pan.x -= 1;
    if (keys_[GLFW_KEY_D] || keys_[GLFW_KEY_RIGHT]) pan.x += 1;
    if (keys_[GLFW_KEY_W] || keys_[GLFW_KEY_UP]) pan.y += 1;
    if (keys_[GLFW_KEY_S] || keys_[GLFW_KEY_DOWN]) pan.y -= 1;
    if (pan != glm::vec2(0.f)) camera_.PanKm(pan * dt * 0.8f);
    if (keys_[GLFW_KEY_Q]) camera_.Orbit(-dt * 1.2f, 0);
    if (keys_[GLFW_KEY_E]) camera_.Orbit(dt * 1.2f, 0);
    if (keys_[GLFW_KEY_R]) camera_.Orbit(0, dt * 0.8f);
    if (keys_[GLFW_KEY_F]) camera_.Orbit(0, -dt * 0.8f);
    if (keys_[GLFW_KEY_EQUAL] || keys_[GLFW_KEY_KP_ADD]) camera_.ZoomAt(mouse_, std::pow(0.3f, dt));
    if (keys_[GLFW_KEY_MINUS] || keys_[GLFW_KEY_KP_SUBTRACT]) {
      camera_.ZoomAt(mouse_, std::pow(1.f / 0.3f, dt));
    }
  }
  camera_.Update(dt);
  UpdateHover();

  // Animate extrusion heights and highlights towards their targets.
  const float k = 1.f - std::exp(-dt * 7.f);
  const int dp = DisplayParent();
  const float D = ExtrudeScale();
  ui::DashboardModel m;
  m.data = &data_;
  m.tree = &tree_;
  m.results = results_.get();
  m.mode = mode_;
  const std::vector<int> path = tree_.Path(dp);
  for (int ancestor : path) {
    LayerInfo* layer = LayerForChildren(ancestor);
    if (!layer) continue;
    const bool extruded = ancestor == dp;
    for (int r : layer->regions) {
      float target;
      if (extruded) {
        double progress = 0;
        if (mode_ == ui::ColorMode::kReferendum && !data_.info().referendums.empty()) {
          progress = results_->ReferendumTally(data_.info().referendums[0].id, r).Progress();
        } else if (const election::Race* race = results_->RaceForRegion(r)) {
          progress = results_->RaceTally(*race, r).Progress();
        }
        target = D * (0.025f + 0.05f * static_cast<float>(progress));
        if (auto p = pulses_.find(r); p != pulses_.end()) target *= 1 + 0.45f * p->second;
        if (r == hover_) target *= 1.25f;
        if (r == focus_) target *= 1.5f;
      } else {
        target = D * 0.0012f;
      }
      float& h = heights_[r];
      h += (target - h) * k;
      float& hl = highlights_[r];
      float hl_target = r == hover_ ? 1.f : (r == focus_ ? 0.6f : 0.f);
      if (auto p = pulses_.find(r); p != pulses_.end()) hl_target = std::max(hl_target, 0.9f * p->second);
      hl += (hl_target - hl) * std::min(1.f, k * 2);
    }
  }
}

void App::BuildFrame(render::FrameInput* in, ui::DashboardModel* m) {
  const VkExtent2D e = renderer_.extent();
  m->width = static_cast<int>(e.width);
  m->height = static_cast<int>(e.height);
  m->data = &data_;
  m->tree = &tree_;
  m->results = results_.get();
  m->focus = focus_;
  m->hover = hover_;
  m->mouse_x = mouse_.x;
  m->mouse_y = mouse_.y;
  m->mode = mode_;
  m->lang = lang_;
  m->show_help = show_help_;
  m->fps = fps_;
  m->gpu = ctx_.device_name();
  m->source = source_->Describe();
  m->source_error = source_error_;
  m->now = std::chrono::system_clock::now();
  m->dt = frame_dt_;
  m->project = [this](int region, float* x, float* y) { return ProjectRegion(region, x, y); };
  m->visible_regions = VisibleRegions();
  m->clock_minutes = election::MinutesAfterClose(snapshot_->updated_at);
  if (sim_) m->clock_minutes = sim_->clock_minutes();
  m->simulated = sim_ != nullptr;
  m->sim_speed = sim_ ? sim_->speed() : 0;
  m->sim_paused = sim_ && sim_->paused();
  m->race_status = &race_status_;
  m->news = &news_view_;
  m->show_news = show_news_;
  m->pinned = pinned_;
  m->chart = chart_;
  m->history = &history_;
  m->chart_race = chart_race_;
  m->pip = pip_;
  if (opt_.headless && hover_ >= 0) {
    // Place the synthetic cursor at the hovered region's label.
    const geo::Region& h = tree_.region(hover_);
    glm::vec2 s;
    if (camera_.WorldToScreen(glm::vec3(h.label, heights_[hover_]), &s)) {
      m->mouse_x = s.x;
      m->mouse_y = s.y;
    }
  }

  // Frames around the outlying islands drawn as insets (not to position).
  int inset_count = 0;
  const geo::Inset* insets = geo::Projection::insets(&inset_count);
  for (int i = 0; i < inset_count; ++i) {
    const int county = tree_.FindByCode(insets[i].county_code);
    if (county < 0) continue;
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (int town : tree_.region(county).children) {
      if (tree_.region(town).code == insets[i].kept_town) continue;
      lo = glm::min(lo, tree_.region(town).min);
      hi = glm::max(hi, tree_.region(town).max);
    }
    const glm::vec2 pad = glm::max((hi - lo) * 0.12f, glm::vec2(2.f));
    lo -= pad;
    hi += pad;
    ui::InsetFrame frame;
    frame.region = county;
    const glm::vec2 corners[4] = {{lo.x, hi.y}, {hi.x, hi.y}, {hi.x, lo.y}, {lo.x, lo.y}};
    bool ok = true;
    for (int k = 0; k < 4; ++k) {
      glm::vec2 s;
      ok &= camera_.WorldToScreen(glm::vec3(corners[k], 0.f), &s);
      frame.x[k] = s.x;
      frame.y[k] = s.y;
    }
    if (ok) m->insets.push_back(frame);
  }

  const CameraPose& pose = camera_.pose();
  render::FrameUniforms& u = in->uniforms;
  u.view_proj = camera_.ViewProj();
  const glm::vec3 eye = camera_.Eye();
  u.eye = glm::vec4(eye, 0.f);
  u.light_dir = glm::vec4(glm::normalize(glm::vec3(-0.45f, 0.55f, 0.85f)), 0.42f);
  const float extent = pose.distance * 5.f + 50.f;
  u.ground = glm::vec4(pose.target - glm::vec2(extent), pose.target + glm::vec2(extent));
  in->clear_color = glm::vec4(0.015f, 0.035f, 0.065f, 1.f);
  u.fog = glm::vec4(0.015f, 0.035f, 0.065f, 1.f / (pose.distance * 3.2f));
  const float spacing = std::pow(10.f, std::floor(std::log10(std::max(0.05f, pose.distance / 6.f))));
  u.sea = glm::vec4(0.03f, 0.075f, 0.125f, spacing);

  const int dp = DisplayParent();
  const float D = ExtrudeScale();
  const std::vector<int> path = tree_.Path(dp);
  for (size_t level = 0; level < path.size(); ++level) {
    const int ancestor = path[level];
    LayerInfo* layer = LayerForChildren(ancestor);
    if (!layer) continue;
    const bool extruded = ancestor == dp;
    const int hidden = level + 1 < path.size() ? path[level + 1] : -1;
    for (size_t slot = 0; slot < layer->regions.size(); ++slot) {
      const int r = layer->regions[slot];
      float strength = 0;
      const uint32_t base = ui::Dashboard::RegionColor(*m, r, &strength);
      uint32_t color = ui::MixColor(kNeutral, base, strength);
      float alpha = 1.f;
      if (!extruded) {
        color = ui::MixColor(0xFF101B28, color, 0.55f);
        alpha = r == hidden ? 0.f : 0.85f;
      } else if (tree_.region(focus_).children.empty() && r != focus_) {
        color = ui::MixColor(0xFF101B28, color, 0.75f);  // a village is selected
      }
      render::RegionStyle& s = layer->styles[slot];
      // Cross-fade colour changes (e.g. a new leader) instead of snapping.
      const glm::vec3 target_rgb = glm::vec3(ToVec4(color));
      auto [cit, fresh] = shown_color_.try_emplace(r, target_rgb);
      cit->second += (target_rgb - cit->second) * (1.f - std::exp(-frame_dt_ * 3.5f));
      glm::vec3 rgb = cit->second;
      if (auto p = pulses_.find(r); p != pulses_.end()) rgb = glm::mix(rgb, glm::vec3(1.f), 0.14f * p->second);
      s.color = glm::vec4(rgb, alpha);
      s.params = glm::vec4(heights_[r], highlights_[r], 0.f, 0.f);
    }
    render::LayerDraw draw;
    draw.layer = layer->gpu_layer;
    draw.styles = &layer->styles;
    draw.outlines = true;
    draw.outline_alpha = extruded ? 0.95f : 0.45f;
    draw.outline_brightness = extruded ? 0.55f : 0.15f;
    in->layers.push_back(draw);

    if (!extruded) continue;
    // Labels + 3D vote bars for the extruded regions.
    const bool referendum = mode_ == ui::ColorMode::kReferendum;
    for (int r : layer->regions) {
      const geo::Region& reg = tree_.region(r);
      const float h = heights_[r];
      glm::vec2 s;
      if (camera_.WorldToScreen(glm::vec3(reg.label, h), &s)) {
        m->labels.push_back({s.x, s.y + 5.f, r, reg.area_km2 + (r == hover_ ? 1e9f : 0.f)});
      }
      std::vector<std::pair<float, uint32_t>> bars;  // share, colour
      std::vector<int64_t> bar_keys;
      if (referendum && !data_.info().referendums.empty()) {
        const auto& t = results_->ReferendumTally(data_.info().referendums[0].id, r);
        const int64_t total = t.agree + t.disagree;
        if (total > 0) {
          bars.push_back({static_cast<float>(t.agree) / total, 0xFF3FA7D6});
          bars.push_back({static_cast<float>(t.disagree) / total, 0xFFE4572E});
        }
      } else if (mode_ == ui::ColorMode::kLeader) {
        if (const election::Race* race = results_->RaceForRegion(r)) {
          const election::Tally& t = results_->RaceTally(*race, r);
          if (t.TotalVotes() > 0) {
            const std::vector<int> rank = t.Ranking();
            // Top three in stable candidate order so bars grow/shrink in place
            // when the ranking changes.
            std::vector<int> top(rank.begin(), rank.begin() + std::min<size_t>(3, rank.size()));
            std::sort(top.begin(), top.end());
            for (int cand : top) {
              bars.push_back({static_cast<float>(t.Share(cand)),
                              data_.party(race->candidates[cand].party).color});
              bar_keys.push_back(static_cast<int64_t>(r) * 64 + cand);
            }
          }
        }
      }
      if (bars.empty()) continue;
      const float w = std::clamp(std::sqrt(std::max(0.01f, reg.area_km2)) * 0.16f, D * 0.004f,
                                 D * 0.018f);
      const float gap = w * 0.3f;
      const float total_w = bars.size() * w + (bars.size() - 1) * gap;
      for (size_t k = 0; k < bars.size(); ++k) {
        render::BarInstance b;
        const float x = reg.label.x - total_w / 2 + w / 2 + k * (w + gap);
        b.base = glm::vec4(x, reg.label.y, h, w);
        b.color = ToVec4(bars[k].second);
        float height = std::max(0.002f * D, bars[k].first * D * 0.11f);
        if (k < bar_keys.size()) {
          auto [bit, fresh] = bar_anim_.try_emplace(bar_keys[k], 0.f);
          bit->second += (height - bit->second) * (1.f - std::exp(-frame_dt_ * 4.f));
          height = bit->second;
        }
        b.size = glm::vec4(height, highlights_[r], w, 0.f);
        in->bars.push_back(b);
      }
    }
  }
}

bool App::RenderFrame(double) {
  render::FrameInput in;
  ui::DashboardModel m;
  BuildFrame(&in, &m);
  BuildEffects(&in, &m, ExtrudeScale());
  dashboard_->Render(m, overlay_.data());
  in.overlay = overlay_.data();
  in.overlay_version = ++overlay_version_;
  const auto result = renderer_.DrawFrame(in);
  return result == render::Renderer::FrameResult::kOk;
}

int App::SaveScreenshot(const std::string& path, bool quiet) {
  std::vector<uint8_t> rgba;
  if (!renderer_.ReadPixels(&rgba)) {
    std::fprintf(stderr, "screenshot readback failed\n");
    return 1;
  }
  const VkExtent2D e = renderer_.extent();
  for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
  if (!stbi_write_png(path.c_str(), static_cast<int>(e.width), static_cast<int>(e.height), 4,
                      rgba.data(), static_cast<int>(e.width) * 4)) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    return 1;
  }
  if (!quiet) std::printf("wrote %s (%ux%u)\n", path.c_str(), e.width, e.height);
  return 0;
}

void App::OnResize() {
  resize_pending_ = false;
  int w = 0, h = 0;
  glfwGetFramebufferSize(window_, &w, &h);
  while (w == 0 || h == 0) {  // minimised
    glfwWaitEvents();
    glfwGetFramebufferSize(window_, &w, &h);
  }
  std::string error;
  if (!renderer_.Resize(w, h, &error)) {
    std::fprintf(stderr, "resize failed: %s\n", error.c_str());
    return;
  }
  const VkExtent2D e = renderer_.extent();
  camera_.SetViewport(static_cast<float>(e.width), static_cast<float>(e.height));
  overlay_.assign(size_t{e.width} * e.height * 4, 0);
}

void App::OnMouseButton(int button, int action, int) {
  if (action == GLFW_PRESS) {
    drag_button_ = button;
    press_pos_ = mouse_;
    dragged_ = false;
    return;
  }
  if (action != GLFW_RELEASE || button != drag_button_) return;
  drag_button_ = -1;
  if (dragged_) return;
  if (button == GLFW_MOUSE_BUTTON_LEFT && HandleOverlayClick()) return;
  if (chart_ != ui::ChartKind::kMap && dashboard_->Captures(mouse_.x, mouse_.y)) return;
  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    const int hit = PickRegion(mouse_);
    if (hit >= 0 && hit != focus_) SetFocus(hit, true);
  } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    if (tree_.region(focus_).parent >= 0) SetFocus(tree_.region(focus_).parent, true);
  }
}

void App::OnCursor(double x, double y) {
  // Convert window coordinates to framebuffer pixels (HiDPI).
  int ww = 1, wh = 1, fw = 1, fh = 1;
  glfwGetWindowSize(window_, &ww, &wh);
  glfwGetFramebufferSize(window_, &fw, &fh);
  const glm::vec2 pos(static_cast<float>(x) * fw / std::max(1, ww),
                      static_cast<float>(y) * fh / std::max(1, wh));
  const glm::vec2 prev = mouse_;
  mouse_ = pos;
  if (drag_button_ < 0) return;
  if (glm::length(mouse_ - press_pos_) > 4.f) dragged_ = true;
  if (!dragged_) return;
  if (dashboard_->Captures(press_pos_.x, press_pos_.y)) return;  // drags on charts/PiP
  if (drag_button_ == GLFW_MOUSE_BUTTON_LEFT) {
    camera_.PanScreen(prev, mouse_);
  } else {
    const glm::vec2 d = mouse_ - prev;
    camera_.Orbit(d.x * 0.006f, d.y * 0.004f);
  }
}

void App::OnScroll(double, double dy) {
  if (dashboard_->Captures(mouse_.x, mouse_.y)) return;
  if (mouse_.x >= ui::Dashboard::Layout(renderer_.extent().width, renderer_.extent().height).panel_x) {
    return;
  }
  camera_.ZoomAt(mouse_, std::pow(0.85f, static_cast<float>(dy)));
}

void App::OnKey(int key, int action, int) {
  if (key >= 0 && key < 512) keys_[key] = action != GLFW_RELEASE;
  if (action != GLFW_PRESS) return;
  switch (key) {
    case GLFW_KEY_ESCAPE:
    case GLFW_KEY_BACKSPACE:
      if (chart_ != ui::ChartKind::kMap) {
        SetChart(ui::ChartKind::kMap);  // secondary views return to the map first
      } else if (tree_.region(focus_).parent >= 0) {
        SetFocus(tree_.region(focus_).parent, true);
      }
      break;
    case GLFW_KEY_M:
      SetChart(ui::ChartKind::kMap);
      break;
    case GLFW_KEY_F2:
    case GLFW_KEY_F3:
    case GLFW_KEY_F4:
    case GLFW_KEY_F5:
    case GLFW_KEY_F6: {
      const auto k = static_cast<ui::ChartKind>(key - GLFW_KEY_F2 + 1);
      SetChart(chart_ == k ? ui::ChartKind::kMap : k);  // same key toggles back
      break;
    }
    case GLFW_KEY_G:
      SetChart(static_cast<ui::ChartKind>((static_cast<int>(chart_) + 1) %
                                          static_cast<int>(ui::ChartKind::kCount)));
      break;
    case GLFW_KEY_I:
      pip_ = !pip_;
      break;
    case GLFW_KEY_V:
      music_.Toggle();
      break;
    case GLFW_KEY_LEFT:
    case GLFW_KEY_RIGHT:
      if (chart_ == ui::ChartKind::kTrend) CycleChartRace(key == GLFW_KEY_RIGHT ? 1 : -1);
      break;
    case GLFW_KEY_1:
    case GLFW_KEY_2:
    case GLFW_KEY_3:
    case GLFW_KEY_4:
      mode_ = static_cast<ui::ColorMode>(key - GLFW_KEY_1);
      break;
    case GLFW_KEY_L:
      lang_ = ui::NextLang(lang_);
      glfwSetWindowTitle(window_,
                         (std::string(ui::Tr(lang_, "window.title")) + " · twn_election").c_str());
      break;
    case GLFW_KEY_H:
    case GLFW_KEY_F1:
      show_help_ = !show_help_;
      break;
    case GLFW_KEY_HOME:
      SetFocus(pinned_ >= 0 ? pinned_ : 0, true);
      break;
    case GLFW_KEY_P:
      TogglePin();
      break;
    case GLFW_KEY_N:
      show_news_ = !show_news_;
      break;
    case GLFW_KEY_SPACE:
      if (sim_) sim_->set_paused(!sim_->paused());
      break;
    case GLFW_KEY_COMMA:
      if (sim_) sim_->SlowDown();
      break;
    case GLFW_KEY_PAGE_DOWN:
      if (sim_) sim_->SlowDown();
      break;
    case GLFW_KEY_PERIOD:
      if (sim_) sim_->SpeedUp();
      break;
    case GLFW_KEY_PAGE_UP:
      if (sim_) sim_->SpeedUp();
      break;
    case GLFW_KEY_F7:
      RestartSimulation();
      break;
    case GLFW_KEY_LEFT_BRACKET:
      if (sim_) SeekSimulation(sim_->clock_minutes() - 30);
      break;
    case GLFW_KEY_RIGHT_BRACKET:
      if (sim_) SeekSimulation(sim_->clock_minutes() + 30);
      break;
    default:
      break;
  }
}

}  // namespace twn::app
