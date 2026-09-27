// Election-night behaviour of the app: turning results updates into events,
// map effects, news items and database records; pinning the home region.

#include <algorithm>
#include <cmath>

#include "glm/common.hpp"
#include "src/app/app.h"

namespace twn::app {
namespace {

constexpr float kPi = 3.14159265f;

float Ease(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

uint32_t Lighten(uint32_t argb, float t) {
  auto ch = [&](int shift) {
    const float v = ((argb >> shift) & 0xFF) * (1 - t) + 255 * t;
    return static_cast<uint32_t>(v) << shift;
  };
  return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

}  // namespace

std::vector<int> App::VisibleRegions() const {
  return tree_.region(DisplayParent()).children;
}

bool App::ProjectRegion(int region, float* x, float* y) const {
  if (region < 0 || region >= tree_.size()) return false;
  auto it = heights_.find(region);
  const float h = it == heights_.end() ? 0.f : it->second;
  glm::vec2 s;
  if (!camera_.WorldToScreen(glm::vec3(tree_.region(region).label, h), &s)) return false;
  *x = s.x;
  *y = s.y;
  return true;
}

void App::OnResults(double now) {
  const election::EventBatch batch = tracker_.Update(*results_, data_.races());
  RebuildRaceStatus(0);
  if (batch.baseline) return;
  ui::DashboardModel m;
  m.data = &data_;
  m.tree = &tree_;
  m.results = results_.get();
  m.focus = focus_;
  m.visible_regions = VisibleRegions();
  dashboard_->PushBatch(batch, election::MinutesAfterClose(snapshot_->updated_at), m);
  SpawnEventEffects(batch);
  SubmitEventNews(batch);
  RecordToDatabase(batch);
  (void)now;
}

void App::RebuildRaceStatus(float dt) {
  for (auto& [id, st] : race_status_) st.called_age += dt;
  if (dt > 0) return;
  for (const election::Race& race : data_.races()) {
    ui::RaceStatus& st = race_status_[race.id];
    st.declared = st.conceded = -1;
    for (const election::Declaration& d : snapshot_->declarations) {
      if (d.race_id != race.id) continue;
      const int who = race.CandidateIndex(d.candidate_id);
      if (d.type == election::Declaration::Type::kVictory) st.declared = who;
      else st.conceded = who;
    }
    const int county = tree_.FindByCode(race.county_code);
    const election::Tally& t = results_->RaceTally(race, county);
    const int called = tracker_.IsCalled(race.id) && t.TotalVotes() > 0 ? t.Leader() : -1;
    if (called != st.called) {
      st.called = called;
      st.called_age = 0;
    }
  }
}

void App::SpawnEventEffects(const election::EventBatch& batch) {
  const float D = ExtrudeScale();
  const std::vector<int> visible = VisibleRegions();
  auto is_visible = [&](int r) { return std::find(visible.begin(), visible.end(), r) != visible.end(); };

  // New votes: pulse + a beam of light, strongest batches first.
  std::vector<election::RegionDelta> deltas;
  for (const auto& d : batch.deltas) {
    if (is_visible(d.region)) deltas.push_back(d);
  }
  std::sort(deltas.begin(), deltas.end(),
            [](const auto& a, const auto& b) { return a.votes_added > b.votes_added; });
  if (deltas.size() > 40) deltas.resize(40);
  for (const auto& d : deltas) {
    const float mag = std::log10(1.f + static_cast<float>(d.votes_added));
    pulses_[d.region] = std::min(0.8f, pulses_[d.region] + 0.1f + 0.08f * mag);
    uint32_t color = 0xFFBFE3FF;
    if (const election::Race* race = results_->RaceForRegion(d.region)) {
      const election::Tally& t = results_->RaceTally(*race, d.region);
      if (t.TotalVotes() > 0) color = Lighten(data_.party(race->candidates[t.Leader()].party).color, 0.45f);
    }
    MapEffect e;
    e.kind = MapEffect::Kind::kBeam;
    e.region = d.region;
    e.color = color;
    e.life = 1.1f;
    e.size = D * 0.012f * (0.5f + 0.25f * mag);
    effects_.push_back(e);
  }
  // Local lead flips: ripples in the new leader's colour.
  for (const auto& f : batch.flips) {
    if (!is_visible(f.region)) continue;
    pulses_[f.region] = 1.f;
    for (int k = 0; k < 2; ++k) {
      MapEffect e;
      e.region = f.region;
      e.color = data_.party(f.race->candidates[f.leader].party).color;
      e.delay = 0.3f * k;
      e.life = 1.4f;
      e.size = std::max(D * 0.03f, std::sqrt(tree_.region(f.region).area_km2) * 0.8f);
      effects_.push_back(e);
    }
  }
  // Race-level events at the county (or at the view when inside it).
  for (const election::ElectionEvent& ev : batch.events) {
    const int county = tree_.FindByCode(ev.race->county_code);
    int anchor = -1;
    if (is_visible(county)) anchor = county;
    else if (tree_.region(focus_).county_code == ev.race->county_code) anchor = DisplayParent();
    if (anchor < 0 || ev.leader < 0) continue;
    const uint32_t leader_color = data_.party(ev.race->candidates[ev.leader].party).color;
    int rings = 0;
    uint32_t color = leader_color;
    switch (ev.type) {
      case election::EventType::kLeadChange: rings = 3; break;
      case election::EventType::kVictoryDeclared: rings = 2; color = 0xFFB07CE0; break;
      case election::EventType::kConcession: rings = 1; color = 0xFF9FB0C3; break;
      case election::EventType::kCalled: rings = 4; color = 0xFFF5C542; break;
      case election::EventType::kIncumbentTrailing: rings = 2; color = 0xFFE17055; break;
      case election::EventType::kFinal: rings = 2; color = 0xFF2ECC71; break;
      default: break;
    }
    pulses_[anchor] = 1.f;
    const float size = std::max(D * 0.06f, std::sqrt(tree_.region(anchor).area_km2) * 1.1f);
    for (int k = 0; k < rings; ++k) {
      MapEffect e;
      e.region = anchor;
      e.color = color;
      e.delay = 0.28f * k;
      e.life = 1.8f;
      e.size = size;
      effects_.push_back(e);
    }
    if (ev.type == election::EventType::kCalled || ev.type == election::EventType::kVictoryDeclared) {
      MapEffect e;
      e.kind = MapEffect::Kind::kBeam;
      e.region = anchor;
      e.color = color;
      e.life = 2.4f;
      e.size = D * 0.05f;
      effects_.push_back(e);
    }
  }
  if (effects_.size() > 400) effects_.erase(effects_.begin(), effects_.end() - 400);
}

void App::SubmitEventNews(const election::EventBatch& batch) {
  if (!news_) return;
  using election::EventType;
  using store::Sentiment;
  for (const election::ElectionEvent& ev : batch.events) {
    if (ev.type == EventType::kFirstReturns || ev.leader < 0) continue;
    if (ev.type == EventType::kLeadChange && ev.progress < 0.08) continue;
    const election::Race& race = *ev.race;
    auto id = [&](int i) { return i >= 0 ? race.candidates[i].id : std::string(); };
    auto name = [&](int i) { return i >= 0 ? race.candidates[i].name_zh : std::string("?"); };
    const std::string title = ui::Fmt(
        ui::Tr(ui::Lang::kZhTW, std::string("ev.") + election::EventKey(ev.type) + ".text"),
        {race.TitleZh(), name(ev.leader), name(ev.previous), ui::FormatThousands(ev.margin)});
    std::vector<store::Assessment> as;
    switch (ev.type) {
      case EventType::kLeadChange:
        as = {{id(ev.leader), Sentiment::kGood, "反超領先"}, {id(ev.previous), Sentiment::kBad, "被反超"}};
        break;
      case EventType::kVictoryDeclared:
        as = {{id(ev.leader), Sentiment::kGood, "宣布勝選"}};
        break;
      case EventType::kConcession:
        as = {{id(ev.leader), Sentiment::kBad, "承認敗選"}};
        if (ev.previous >= 0 && ev.previous != ev.leader) {
          as.push_back({id(ev.previous), Sentiment::kGood, "對手承認敗選"});
        }
        break;
      case EventType::kCalled:
        as = {{id(ev.leader), Sentiment::kGood, "當選確定"}};
        break;
      case EventType::kIncumbentTrailing:
        as = {{id(ev.previous), Sentiment::kBad, "現任落後"}, {id(ev.leader), Sentiment::kGood, "領先現任"}};
        break;
      case EventType::kCloseRace:
        as = {{id(ev.leader), Sentiment::kNeutral, "差距膠著"}, {id(ev.previous), Sentiment::kNeutral, "差距膠著"}};
        break;
      case EventType::kFinal:
        as = {{id(ev.leader), Sentiment::kGood, "開票完畢勝出"}};
        break;
      default:
        break;
    }
    const std::string url = std::string(snapshot_->simulated ? "sim://" : "event://") + race.id +
                            "/" + election::EventKey(ev.type) + "/" + ev.time_label + "/" +
                            id(ev.leader);
    news_->Submit(news::ArticleFromEvent(title, url, snapshot_->updated_at, as, snapshot_->simulated));
  }
}

void App::RecordToDatabase(const election::EventBatch& batch) {
  if (!db_.is_open()) return;
  const bool sim = snapshot_->simulated;
  for (const election::ElectionEvent& ev : batch.events) {
    const election::Race& race = *ev.race;
    db_.RecordEvent(snapshot_->updated_at, election::EventKey(ev.type), race.id,
                    ev.leader >= 0 ? race.candidates[ev.leader].id : "",
                    ev.previous >= 0 ? race.candidates[ev.previous].id : "", ev.margin, ev.progress,
                    sim);
  }
  // Race totals at most once per snapshot timestamp (minute resolution).
  if (snapshot_->updated_at == last_recorded_time_ || batch.votes_added == 0) return;
  last_recorded_time_ = snapshot_->updated_at;
  std::vector<store::Database::RaceTotal> totals;
  for (const election::Race& race : data_.races()) {
    const election::Tally& t = results_->RaceTally(race, tree_.FindByCode(race.county_code));
    for (size_t i = 0; i < race.candidates.size(); ++i) {
      totals.push_back({race.id, race.candidates[i].id, i < t.votes.size() ? t.votes[i] : 0,
                        t.units_counted, t.units_total});
    }
  }
  db_.RecordTotals(snapshot_->updated_at, sim, totals);
}

void App::UpdateEffects(float dt) {
  for (MapEffect& e : effects_) e.age += dt;
  effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                [](const MapEffect& e) { return e.age > e.delay + e.life; }),
                 effects_.end());
  for (auto it = pulses_.begin(); it != pulses_.end();) {
    it->second -= dt * 1.4f;
    if (it->second <= 0) it = pulses_.erase(it);
    else ++it;
  }
  RebuildRaceStatus(dt);
}

void App::BuildEffects(render::FrameInput* in, ui::DashboardModel* m, float D) {
  for (const MapEffect& e : effects_) {
    const float t = (e.age - e.delay) / e.life;
    if (t < 0 || t > 1) continue;
    const geo::Region& r = tree_.region(e.region);
    const float h = heights_.count(e.region) ? heights_[e.region] : 0.f;
    if (e.kind == MapEffect::Kind::kRing) {
      ui::RingEffect ring;
      ring.color = e.color;
      ring.alpha = (1 - t) * 0.95f;
      ring.width = 3.f * (1 - t) + 1.f;
      const float radius = e.size * (0.15f + 0.85f * Ease(t));
      bool ok = true;
      for (int k = 0; k <= 48 && ok; ++k) {
        const float a = 2 * kPi * k / 48;
        glm::vec2 s;
        ok = camera_.WorldToScreen(
            glm::vec3(r.label + glm::vec2(std::cos(a), std::sin(a)) * radius, h + 0.02f), &s);
        ring.points.push_back({s.x, s.y});
      }
      if (ok) m->rings.push_back(std::move(ring));
    } else {
      render::BarInstance b;
      const float w = std::max(D * 0.0025f, e.size * 0.08f);
      b.base = glm::vec4(r.label, h, w);
      const float a = t < 0.15f ? t / 0.15f : 1 - (t - 0.15f) / 0.85f;
      b.color = glm::vec4(((e.color >> 16) & 0xFF) / 255.f, ((e.color >> 8) & 0xFF) / 255.f,
                          (e.color & 0xFF) / 255.f, 0.75f * a);
      b.size = glm::vec4(e.size * (0.3f + 3.2f * Ease(t * 1.6f)), 0.6f, w, 0.f);
      in->bars.push_back(b);
    }
  }
  // Persistent beacons over counties whose winner is projected.
  if (tree_.region(DisplayParent()).level == geo::Level::kNation) {
    for (const election::Race& race : data_.races()) {
      auto it = race_status_.find(race.id);
      if (it == race_status_.end() || it->second.called < 0) continue;
      const int county = tree_.FindByCode(race.county_code);
      const geo::Region& r = tree_.region(county);
      const uint32_t c = data_.party(race.candidates[it->second.called].party).color;
      const float pulse = 0.5f + 0.5f * std::sin(it->second.called_age * 2.2f);
      render::BarInstance b;
      const float w = D * 0.004f;
      b.base = glm::vec4(r.label + glm::vec2(0, -D * 0.012f), heights_[county], w);
      b.color = glm::vec4(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f,
                          0.35f + 0.25f * pulse);
      b.size = glm::vec4(D * (0.06f + 0.01f * pulse) * Ease(it->second.called_age / 1.2f), 0.8f, w, 0);
      in->bars.push_back(b);
    }
  }
}

void App::GenerateMockNews(double now) {
  if (!mock_news_ || !news_) return;
  // Simulated clock when simulating; otherwise one mock minute per real second.
  const double minute = sim_ ? sim_->clock_minutes() : now - 180;
  if (minute < mock_minute_) mock_minute_ = minute;  // rewound
  if (minute - mock_minute_ < 1) return;
  for (auto& m : mock_news_->Between(mock_minute_, minute, opt_.mock_news_per_hour)) {
    news_->SubmitArticle(std::move(m.article));
  }
  mock_minute_ = minute;
}

void App::RefreshNews(double now) {
  if (!db_.is_open() || now < next_news_refresh_) return;
  next_news_refresh_ = now + 0.5;
  news_view_.enabled = news_ != nullptr;
  news_view_.status = news_ ? news_->Status() : "";
  news_view_.total = db_.TotalCounts();
  news_view_.by_candidate = db_.CountsByCandidate();
  news_view_.latest = db_.LatestNews(12);
  // Animate articles classified since the last refresh (not the initial load).
  int64_t newest = last_news_id_;
  for (auto it = news_view_.latest.rbegin(); it != news_view_.latest.rend(); ++it) {
    if (it->article.id <= last_news_id_) continue;
    newest = std::max(newest, it->article.id);
    if (last_news_id_ < 0 || it->assessments.empty()) continue;
    ui::DashboardModel m;
    m.data = &data_;
    m.tree = &tree_;
    m.focus = focus_;
    m.visible_regions = VisibleRegions();
    dashboard_->PushNews(*it, m);
    // Map: a soft ripple on the subject's county in good/bad colour.
    const election::Race* race = nullptr;
    if (data_.CandidateById(it->assessments.front().candidate_id, &race) && race) {
      const int county = tree_.FindByCode(race->county_code);
      const std::vector<int> visible = VisibleRegions();
      const int anchor = std::find(visible.begin(), visible.end(), county) != visible.end()
                             ? county
                             : (tree_.region(focus_).county_code == race->county_code ? DisplayParent() : -1);
      if (anchor >= 0) {
        MapEffect e;
        e.region = anchor;
        e.color = it->assessments.front().sentiment == store::Sentiment::kGood  ? 0xFF2ECC71
                  : it->assessments.front().sentiment == store::Sentiment::kBad ? 0xFFE74C3C
                                                                                : 0xFFB0BEC5;
        e.life = 1.6f;
        e.size = std::max(ExtrudeScale() * 0.04f, std::sqrt(tree_.region(anchor).area_km2) * 0.9f);
        effects_.push_back(e);
      }
    }
  }
  last_news_id_ = std::max<int64_t>(newest, 0);
}

void App::TogglePin() {
  if (!db_.is_open()) return;
  if (pinned_ == focus_) {
    db_.DeleteSetting("home_region");
    pinned_ = -1;
    dashboard_->Notify(ui::Tr(lang_, "pin.unpinned"));
  } else {
    db_.SetSetting("home_region", tree_.region(focus_).code);
    pinned_ = focus_;
    dashboard_->Notify(std::string(ui::Tr(lang_, "pin.pinned")) + " · " +
                       ui::Localizer(lang_).RegionName(tree_.region(focus_)));
  }
}

void App::SeekSimulation(double minutes) {
  if (!sim_) return;
  const bool backwards = minutes < sim_->clock_minutes();
  sim_->SeekClock(minutes);
  if (backwards) {
    // Rewinding: forget event state so nothing fires twice.
    tracker_.Reset();
    dashboard_->ResetLive();
    effects_.clear();
  }
}

}  // namespace twn::app
