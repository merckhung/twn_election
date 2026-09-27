// Election-night "live layer" of the dashboard: breaking-news banners, map
// call-outs, the event feed, the counting timeline, rolling numbers and the
// small animations that make incoming data visible.
//
// Animation vocabulary (see README "Election night"):
//   * new votes      rolling counters, "+N" floaters, row flashes; on the map
//                    a pulse (height bump + glow) and a light beam per region
//   * lead change    red BREAKING banner, call-out on the region, cards slide
//                    into the new order with a gold flash, map colour
//                    cross-fades with two ripples in the new leader's colour
//   * declaration    banner + call-out; "declared" chip on the card (purple)
//   * concession     banner + call-out; card dims, "conceded" chip
//   * projection     banner, red 當選 stamp slams onto the card, gold beacon
//                    and ripples over the county
//   * steady inflow  every ~2.5 s a call-out on the busiest region shows who
//                    leads whom and by how much

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "src/election/results_source.h"
#include "src/ui/dashboard.h"
#include "src/store/db.h"

namespace twn::ui {

using election::ElectionEvent;
using election::EventType;

namespace {

constexpr SkColor kPanel = SkColorSetARGB(232, 10, 20, 33);
constexpr SkColor kText = SkColorSetRGB(242, 245, 248);
constexpr SkColor kText2 = SkColorSetRGB(160, 176, 195);
constexpr SkColor kText3 = SkColorSetRGB(110, 126, 146);
constexpr SkColor kBreaking = SkColorSetRGB(214, 48, 49);
constexpr SkColor kGold = SkColorSetRGB(245, 197, 66);

SkPaint FillA(SkColor c, float alpha) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  p.setAlphaf(std::clamp(alpha, 0.f, 1.f) * SkColorGetA(c) / 255.f);
  return p;
}

float Ease(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 1 - (1 - t) * (1 - t) * (1 - t);
}

// Alpha envelope: fade in over `in`, hold, fade out over `out`.
float Envelope(float age, float life, float in = 0.25f, float out = 0.5f) {
  if (age < in) return age / in;
  if (age > life - out) return std::max(0.f, (life - age) / out);
  return 1;
}

uint32_t EventColor(EventType t) {
  switch (t) {
    case EventType::kLeadChange: return 0xFFE08E0B;
    case EventType::kVictoryDeclared: return 0xFF9B59B6;
    case EventType::kConcession: return 0xFF7F8C8D;
    case EventType::kCalled: return 0xFFD63031;
    case EventType::kIncumbentTrailing: return 0xFFE17055;
    case EventType::kCloseRace: return 0xFF00B894;
    case EventType::kFinal: return 0xFF2E9E5B;
    default: return 0xFF3A6EA5;
  }
}

}  // namespace

double Dashboard::Roll(const std::string& key, double target, double* added) {
  auto [it, inserted] = nums_.try_emplace(key, AnimNum{target, target});
  AnimNum& n = it->second;
  if (added) *added = inserted ? 0 : std::max(0.0, target - n.target);
  if (target < n.target) n.shown = target;  // clock moved backwards: snap
  n.target = target;
  const double k = 1 - std::exp(-dt_ * 3.5);
  n.shown += (n.target - n.shown) * k;
  if (std::fabs(n.target - n.shown) < 1) n.shown = n.target;
  return n.shown;
}

float Dashboard::Approach(std::unordered_map<std::string, float>& map, const std::string& key,
                          float target, float rate) {
  auto [it, inserted] = map.try_emplace(key, target);
  float& v = it->second;
  v += (target - v) * (1 - std::exp(-dt_ * rate));
  return v;
}

const RaceStatus* Dashboard::StatusOf(const DashboardModel& m, const election::Race& race) const {
  if (!m.race_status) return nullptr;
  auto it = m.race_status->find(race.id);
  return it == m.race_status->end() ? nullptr : &it->second;
}

void Dashboard::ResetLive() {
  pip_queue_.clear();
  toasts_.clear();
  pending_toasts_.clear();
  feed_.clear();
  callouts_.clear();
  floaters_.clear();
  pending_delta_.clear();
  inflow_.clear();
  markers_.clear();
  flash_.clear();
}

void Dashboard::PushBatch(const election::EventBatch& batch, double clock_minutes,
                          const DashboardModel& m) {
  if (clock_minutes >= 0 && batch.votes_added > 0) {
    const size_t bucket = static_cast<size_t>(clock_minutes / 5);
    if (inflow_.size() <= bucket) inflow_.resize(bucket + 1, 0);
    inflow_[bucket] += batch.votes_added;
  }
  for (const auto& d : batch.deltas) pending_delta_[d.region] += d.votes_added;

  const geo::Region& focus = m.tree->region(m.focus);
  for (const ElectionEvent& e : batch.events) {
    feed_.push_front(e);
    if (feed_.size() > 60) feed_.pop_back();
    if (clock_minutes >= 0) markers_.push_back({static_cast<float>(clock_minutes), e.type});
    const std::string key = e.race->id + ":" + std::to_string(e.leader);
    if (e.type == EventType::kLeadChange || e.type == EventType::kCalled ||
        e.type == EventType::kVictoryDeclared) {
      flash_[key] = 1.f;
    }
    flash_["row:" + e.race->id] = 1.f;
    // Early lead changes on a handful of ballots are noise: feed only.
    const bool early_noise = e.type == EventType::kLeadChange && e.progress < 0.08;
    if (e.type != EventType::kFirstReturns && !early_noise) {
      PipItem item;
      item.event = e;
      item.breaking = election::IsBreaking(e.type);
      if (item.breaking) {
        // Breaking items jump ahead of routine ones.
        auto pos = std::find_if(pip_queue_.begin(), pip_queue_.end(),
                                [](const PipItem& p) { return !p.breaking; });
        pip_queue_.insert(pos, item);
      } else {
        pip_queue_.push_back(item);
      }
      while (pip_queue_.size() > 12) pip_queue_.pop_back();
    }
    if (election::IsBreaking(e.type) && !early_noise) pending_toasts_.push_back(e);

    // Call-out anchored on the race's county (nation view) or on the view
    // itself when the user is inside that county.
    int anchor = -1;
    const int county = m.tree->FindByCode(e.race->county_code);
    if (focus.level == geo::Level::kNation) {
      anchor = county;
    } else if (focus.county_code == e.race->county_code) {
      anchor = focus.children.empty() ? m.focus : m.focus;
    }
    if (anchor >= 0 && !early_noise && e.type != EventType::kFirstReturns) {
      Callout c;
      c.kind = CalloutKind::kEvent;
      c.event = e;
      c.region = anchor;
      c.life = election::IsBreaking(e.type) ? 6.f : 4.5f;
      callouts_.push_back(c);
    }
  }
  // Local lead flips inside the current view (townships / villages).
  for (const auto& f : batch.flips) {
    if (flip_budget_ < 1 || std::find(m.visible_regions.begin(), m.visible_regions.end(),
                                      f.region) == m.visible_regions.end()) {
      continue;
    }
    if (m.tree->region(f.region).level == geo::Level::kCounty) continue;  // covered by events
    flip_budget_ -= 1;
    Callout c;
    c.kind = CalloutKind::kFlip;
    c.event.race = f.race;
    c.event.leader = f.leader;
    c.event.previous = f.previous;
    c.region = f.region;
    c.life = 3.2f;
    callouts_.push_back(c);
  }
  // Keep the map readable: at most 5 call-outs, oldest non-breaking first.
  while (callouts_.size() > 4) {
    auto victim = std::find_if(callouts_.begin(), callouts_.end(), [](const Callout& c) {
      return c.kind != CalloutKind::kEvent || !election::IsBreaking(c.event.type);
    });
    callouts_.erase(victim == callouts_.end() ? callouts_.begin() : victim);
  }
}

void Dashboard::PushNews(const store::ClassifiedArticle& article, const DashboardModel& m) {
  for (const store::Assessment& a : article.assessments) flash_["news:" + a.candidate_id] = 1.f;
  if (article.model != "event-rule" && !article.assessments.empty()) {
    PipItem item;
    item.is_news = true;
    item.news = article;
    const election::Race* race = nullptr;
    m.data->CandidateById(article.assessments.front().candidate_id, &race);
    item.event.race = race;
    pip_queue_.push_back(item);
    while (pip_queue_.size() > 12) pip_queue_.pop_back();
  }
  // Event-derived items already have their own banner/call-out; ordinary
  // news gets at most one call-out per ~1.5 s and yields to live events.
  if (article.model == "event-rule" || news_budget_ < 1 || callouts_.size() >= 3) return;
  news_budget_ -= 1;
  const election::Race* race = nullptr;
  const election::Candidate* cand =
      m.data->CandidateById(article.assessments.front().candidate_id, &race);
  if (!cand || !race) return;
  const int county = m.tree->FindByCode(race->county_code);
  const geo::Region& focus = m.tree->region(m.focus);
  int anchor = -1;
  if (std::find(m.visible_regions.begin(), m.visible_regions.end(), county) !=
      m.visible_regions.end()) {
    anchor = county;
  } else if (focus.county_code == race->county_code) {
    anchor = m.focus;
  }
  if (anchor < 0) return;
  Callout c;
  c.kind = CalloutKind::kNews;
  c.news = article;
  c.event.race = race;
  c.event.leader = race->CandidateIndex(cand->id);
  c.region = anchor;
  c.life = 4.2f;
  callouts_.push_back(c);
  while (callouts_.size() > 6) callouts_.erase(callouts_.begin());
}

void Dashboard::UpdateLive(const DashboardModel& m) {
  dt_ = std::clamp(m.dt, 0.f, 0.25f);
  time_ += dt_;
  flip_budget_ = std::min(2.f, flip_budget_ + dt_ * 0.7f);
  news_budget_ = std::min(2.f, news_budget_ + dt_ * 0.65f);
  for (auto& [k, v] : flash_) v = std::max(0.f, v - dt_ * 0.7f);
  for (Toast& t : toasts_) t.age += dt_;
  while (!toasts_.empty() && toasts_.front().age > 5.5f) toasts_.pop_front();
  // With a chart open, one banner at a time keeps the chart title visible.
  const size_t max_toasts = m.chart == ChartKind::kMap ? 2 : 1;
  while (toasts_.size() < max_toasts && !pending_toasts_.empty()) {
    // Collapse bursts: never queue more than 6 banners.
    while (pending_toasts_.size() > 6) pending_toasts_.pop_front();
    if (!toasts_.empty() && toasts_.back().age < 1.2f) break;  // stagger
    toasts_.push_back({pending_toasts_.front(), 0.f});
    pending_toasts_.pop_front();
  }
  for (Callout& c : callouts_) c.age += dt_;
  callouts_.erase(std::remove_if(callouts_.begin(), callouts_.end(),
                                 [](const Callout& c) { return c.age > c.life; }),
                  callouts_.end());
  for (Floater& f : floaters_) f.age += dt_;
  floaters_.erase(std::remove_if(floaters_.begin(), floaters_.end(),
                                 [](const Floater& f) { return f.age > 1.6f; }),
                  floaters_.end());

  // Periodic "who leads whom" call-out on the busiest visible region.
  lead_timer_ += dt_;
  if (lead_timer_ > 2.5f && !pending_delta_.empty()) {
    lead_timer_ = 0;
    int best = -1;
    int64_t best_votes = 0;
    for (int r : m.visible_regions) {
      auto it = pending_delta_.find(r);
      if (it != pending_delta_.end() && it->second > best_votes) {
        best_votes = it->second;
        best = r;
      }
    }
    pending_delta_.clear();
    const election::Race* race = best >= 0 ? m.results->RaceForRegion(best) : nullptr;
    const bool busy = std::any_of(callouts_.begin(), callouts_.end(),
                                  [&](const Callout& c) { return c.region == best; });
    if (race && !busy) {
      const election::Tally& t = m.results->RaceTally(*race, best);
      const std::vector<int> rank = t.Ranking();
      if (t.TotalVotes() > 0 && rank.size() > 1) {
        Callout c;
        c.kind = CalloutKind::kLead;
        c.event.race = race;
        c.event.leader = rank[0];
        c.event.previous = rank[1];
        c.event.margin = t.votes[rank[0]] - t.votes[rank[1]];
        c.event.progress = t.Progress();
        c.region = best;
        c.life = 3.8f;
        callouts_.push_back(c);
      }
    }
  }
}

std::string Dashboard::EventText(const ElectionEvent& e, const DashboardModel& m) const {
  const election::Race& race = *e.race;
  auto name = [&](int i) {
    return i >= 0 && i < static_cast<int>(race.candidates.size())
               ? L_.CandidateName(race.candidates[i])
               : std::string("?");
  };
  std::string text = Fmt(L_.T(std::string("ev.") + election::EventKey(e.type) + ".text"),
                         {L_.RaceTitle(race), name(e.leader), name(e.previous),
                          FormatThousands(e.margin)});
  if (e.after_declaration) text += L_.T("ev.after_declaration");
  (void)m;
  return text;
}

std::string Dashboard::LeadText(const ElectionEvent& e, const DashboardModel& m) const {
  const election::Race& race = *e.race;
  (void)m;
  return Fmt(L_.T("lead.text"), {L_.CandidateName(race.candidates[e.leader]),
                                 L_.CandidateName(race.candidates[e.previous]),
                                 FormatThousands(e.margin)});
}

void Dashboard::DrawRings(SkCanvas* c, const DashboardModel& m) {
  for (const RingEffect& r : m.rings) {
    if (r.points.size() < 3) continue;
    SkPathBuilder b;
    b.moveTo(r.points[0]);
    for (size_t i = 1; i < r.points.size(); ++i) b.lineTo(r.points[i]);
    b.close();
    SkPaint p = FillA(r.color, r.alpha);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(r.width * s_);
    c->drawPath(b.detach(), p);
  }
}

void Dashboard::DrawCallouts(SkCanvas* c, const DashboardModel& m) {
  if (!m.project) return;
  const float panel_x = Layout(m.width, m.height).panel_x;
  std::vector<SkRect> placed;
  for (const Callout& co : callouts_) {
    float ax, ay;
    if (!m.project(co.region, &ax, &ay)) continue;
    if (ax < 0 || ax > panel_x || ay < 0 || ay > m.height) continue;
    const float a = Envelope(co.age, co.life);
    const election::Race& race = *co.event.race;
    const bool breaking = co.kind == CalloutKind::kEvent && election::IsBreaking(co.event.type);
    std::string title = L_.RegionName(m.tree->region(co.region));
    std::string line, sub;
    uint32_t accent = 0xFF3A6EA5;
    if (co.kind == CalloutKind::kEvent) {
      line = EventText(co.event, m);
      // Drop the "臺北市長：" prefix, the title already names the place.
      const std::string prefix = L_.RaceTitle(race);
      if (line.rfind(prefix, 0) == 0) {
        line = line.substr(prefix.size());
        for (const char* sep : {"：", ": "}) {
          if (line.rfind(sep, 0) == 0) line = line.substr(std::strlen(sep));
        }
      }
      title = std::string(L_.T(std::string("ev.") + election::EventKey(co.event.type) + ".title")) +
              " · " + L_.RaceTitle(race);
      accent = EventColor(co.event.type);
      sub = co.event.time_label + "  " +
            Fmt(L_.T("callout.counted"), {std::to_string(static_cast<int>(co.event.progress * 100)) + "%"});
    } else if (co.kind == CalloutKind::kNews) {
      const store::Assessment& a = co.news.assessments.front();
      const bool good = a.sentiment == store::Sentiment::kGood;
      const bool bad = a.sentiment == store::Sentiment::kBad;
      title = std::string(L_.T(good ? "news.good" : bad ? "news.bad" : "news.neutral")) + " · " +
              co.news.article.source;
      line = co.news.digest.empty() ? co.news.article.title : co.news.digest;
      if (co.news.article.simulated && line.rfind("【模擬】", 0) != 0) line = "【模擬】" + line;
      sub = a.reason + (a.reason.empty() ? "" : " · ") + co.news.model;
      accent = good ? 0xFF2ECC71 : bad ? 0xFFE74C3C : 0xFF90A4AE;
    } else if (co.kind == CalloutKind::kFlip) {
      line = Fmt(L_.T("flip.text"), {L_.CandidateName(race.candidates[co.event.leader]),
                                    L_.CandidateName(race.candidates[co.event.previous])});
      accent = 0xFFE08E0B;
    } else {
      line = LeadText(co.event, m);
      sub = Fmt(L_.T("callout.counted"),
                {std::to_string(static_cast<int>(co.event.progress * 100)) + "%"});
      accent = m.data->party(race.candidates[co.event.leader].party).color;
    }
    const SkFont tf = fonts_->Bold(12.5f * s_);
    const SkFont lf = fonts_->Bold(14.5f * s_);
    const SkFont sf = fonts_->Regular(11.5f * s_);
    const float av = 26 * s_;
    const float pad = 9 * s_;
    const float text_w = std::max({TextWidth(tf, title), TextWidth(lf, line), TextWidth(sf, sub)});
    const float w = std::min(360 * s_, text_w + av * 2 + pad * 3 + 6 * s_);
    const float h = (sub.empty() ? 50 : 64) * s_;
    // Rise in, float slightly.
    const float lift = (1 - Ease(co.age / 0.35f)) * 12 * s_;
    float bx = std::clamp(ax - w / 2, 8 * s_, panel_x - w - 8 * s_);
    float by = ay - h - 26 * s_ + lift;
    if (by < 8 * s_) by = ay + 26 * s_;
    SkRect box = SkRect::MakeXYWH(bx, by, w, h);
    // Stack away from earlier call-outs: upwards first, then below the anchor.
    auto overlaps = [&](const SkRect& b) {
      for (const SkRect& p : placed) {
        if (SkRect::Intersects(p, b.makeOutset(4 * s_, 4 * s_))) return true;
      }
      return false;
    };
    for (int tries = 0; tries < 6 && overlaps(box); ++tries) box.offset(0, -(h + 8 * s_));
    if (box.fTop < 8 * s_) {
      box.offsetTo(bx, ay + 26 * s_);
      for (int tries = 0; tries < 6 && overlaps(box); ++tries) box.offset(0, h + 8 * s_);
    }
    placed.push_back(box);
    // Pointer.
    SkPathBuilder tri;
    const float px = std::clamp(ax, box.fLeft + 12 * s_, box.fRight - 12 * s_);
    const bool below = box.fTop > ay;
    const float edge = below ? box.fTop : box.fBottom;
    tri.moveTo(px - 7 * s_, edge);
    tri.lineTo(ax, ay);
    tri.lineTo(px + 7 * s_, edge);
    tri.close();
    c->drawPath(tri.detach(), FillA(breaking ? kBreaking : SkColorSetARGB(255, 60, 80, 105), a * 0.9f));
    c->drawCircle(ax, ay, 4 * s_, FillA(accent, a));
    c->drawRRect(SkRRect::MakeRectXY(box, 9 * s_, 9 * s_), FillA(kPanel, a));
    SkPaint border = FillA(breaking ? kBreaking : accent, a);
    border.setStyle(SkPaint::kStroke_Style);
    border.setStrokeWidth((breaking ? 2.f : 1.3f) * s_);
    c->drawRRect(SkRRect::MakeRectXY(box.makeInset(0.5f, 0.5f), 9 * s_, 9 * s_), border);
    c->saveLayerAlphaf(nullptr, a);
    // Avatars: leader (and the other side, if any).
    float x = box.fLeft + pad;
    const float cy = box.fTop + (h - av) / 2;
    if (co.event.leader >= 0) {
      const auto& cand = race.candidates[co.event.leader];
      avatars_->Draw(c, cand, m.data->party(cand.party), x, cy, av);
      x += av + 3 * s_;
    }
    if (co.event.previous >= 0) {
      const auto& cand = race.candidates[co.event.previous];
      avatars_->Draw(c, cand, m.data->party(cand.party), x, cy, av * 0.8f);
      x += av * 0.8f + 3 * s_;
    }
    x += 4 * s_;
    const float tw = box.fRight - pad - x;
    float ty = box.fTop + pad + 11 * s_;
    if (breaking) {
      const std::string tag = L_.T("breaking");
      const float cw = TextWidth(fonts_->Bold(10.5f * s_), tag) + 8 * s_;
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, ty - 10.5f * s_, cw, 14 * s_), 3 * s_,
                                       3 * s_),
                   FillA(kBreaking, 1));
      DrawText(c, tag, x + 4 * s_, ty, fonts_->Bold(10.5f * s_), SK_ColorWHITE);
      DrawText(c, Ellipsize(tf, title, tw - cw - 6 * s_), x + cw + 6 * s_, ty, tf, kText2);
    } else {
      DrawText(c, Ellipsize(tf, title, tw), x, ty, tf, kText2);
    }
    ty += 19 * s_;
    DrawText(c, Ellipsize(lf, line, tw), x, ty, lf, kText);
    if (!sub.empty()) {
      ty += 16 * s_;
      DrawText(c, Ellipsize(sf, sub, tw), x, ty, sf, kText3);
    }
    c->restore();
  }
}

void Dashboard::DrawToasts(SkCanvas* c, const DashboardModel& m) {
  const float panel_x = Layout(m.width, m.height).panel_x;
  const float left = 590 * s_;
  const float w = std::min(560 * s_, panel_x - left - 16 * s_);
  if (w < 200 * s_) return;
  const float x = left + (panel_x - 16 * s_ - left - w) / 2;
  float y = 16 * s_;
  const float h = 66 * s_;
  for (const Toast& t : toasts_) {
    const float life = 5.5f;
    const float in = Ease(t.age / 0.35f);
    const float out = t.age > life - 0.45f ? Ease((life - t.age) / 0.45f) : 1.f;
    const float a = std::min(in, out);
    const float slide = (1 - a) * -24 * s_;
    const ElectionEvent& e = t.event;
    const election::Race& race = *e.race;
    const SkRect box = SkRect::MakeXYWH(x, y + slide, w, h);
    c->saveLayerAlphaf(nullptr, a);
    c->drawRRect(SkRRect::MakeRectXY(box, 10 * s_, 10 * s_), FillA(kPanel, 1));
    // Red "BREAKING" slab on the left with a shine sweeping across.
    const SkRect slab = SkRect::MakeXYWH(box.fLeft, box.fTop, 84 * s_, h);
    c->save();
    c->clipRRect(SkRRect::MakeRectXY(box, 10 * s_, 10 * s_), true);
    c->drawRect(slab, FillA(kBreaking, 1));
    const float sweep = std::fmod(t.age * 1.3f, 1.6f) * 1.6f - 0.3f;
    c->drawRect(SkRect::MakeXYWH(slab.fLeft + slab.width() * sweep, slab.fTop, 10 * s_, h),
                FillA(SK_ColorWHITE, 0.18f));
    c->drawRect(SkRect::MakeXYWH(box.fLeft, box.fBottom - 3 * s_,
                                 box.width() * std::clamp(t.age / 5.5f, 0.f, 1.f), 3 * s_),
                FillA(EventColor(e.type), 1));
    c->restore();
    DrawText(c, L_.T("breaking"), slab.centerX(), slab.centerY() + 5 * s_, fonts_->Bold(15 * s_),
             SK_ColorWHITE, Align::kCenter);
    SkPaint border = FillA(kBreaking, 1);
    border.setStyle(SkPaint::kStroke_Style);
    border.setStrokeWidth(1.5f * s_);
    c->drawRRect(SkRRect::MakeRectXY(box.makeInset(0.75f, 0.75f), 10 * s_, 10 * s_), border);
    // Avatars.
    float ax = slab.fRight + 10 * s_;
    const float av = 42 * s_;
    if (e.leader >= 0) {
      const auto& cand = race.candidates[e.leader];
      avatars_->Draw(c, cand, m.data->party(cand.party), ax, box.fTop + (h - av) / 2, av);
      ax += av + 4 * s_;
    }
    if (e.previous >= 0 && e.type != EventType::kConcession) {
      const auto& cand = race.candidates[e.previous];
      const float av2 = av * 0.72f;
      avatars_->Draw(c, cand, m.data->party(cand.party), ax, box.fTop + (h - av2) / 2, av2);
      ax += av2 + 4 * s_;
    }
    ax += 6 * s_;
    const float tw = box.fRight - 12 * s_ - ax;
    const std::string title =
        std::string(L_.T(std::string("ev.") + election::EventKey(e.type) + ".title")) + " · " +
        e.time_label;
    DrawText(c, Ellipsize(fonts_->Bold(12.5f * s_), title, tw), ax, box.fTop + 22 * s_,
             fonts_->Bold(12.5f * s_), 0xFF000000 | EventColor(e.type));
    DrawText(c, Ellipsize(fonts_->Bold(17 * s_), EventText(e, m), tw), ax, box.fTop + 46 * s_,
             fonts_->Bold(17 * s_), kText);
    c->restore();
    y += h + 8 * s_;
  }
}

void Dashboard::DrawFloaters(SkCanvas* c) {
  for (const Floater& f : floaters_) {
    const float t = f.age / 1.6f;
    const float a = t < 0.15f ? t / 0.15f : 1 - Ease((t - 0.15f) / 0.85f);
    DrawText(c, f.text, f.x, f.y - Ease(t) * 26 * s_, fonts_->Bold(13 * s_),
             SkColorSetA(f.color, static_cast<U8CPU>(255 * std::clamp(a, 0.f, 1.f))),
             Align::kRight);
  }
}

void Dashboard::DrawStamp(SkCanvas* c, float cx, float cy, float size, float age) {
  // Red seal "當選" slamming down: scales from 2.2x with a flash, then rests.
  const float t = std::clamp(age / 0.35f, 0.f, 1.f);
  const float scale = 1 + (1 - Ease(t)) * 1.2f;
  const float alpha = std::min(1.f, 0.2f + t);
  c->save();
  c->translate(cx, cy);
  c->rotate(-14);
  c->scale(scale, scale);
  const float r = size / 2;
  SkPaint ring = FillA(kBreaking, alpha);
  ring.setStyle(SkPaint::kStroke_Style);
  ring.setStrokeWidth(size * 0.07f);
  c->drawCircle(0, 0, r, ring);
  ring.setStrokeWidth(size * 0.025f);
  c->drawCircle(0, 0, r * 0.82f, ring);
  DrawText(c, L_.T("stamp.elected"), 0, size * 0.13f, fonts_->Bold(size * 0.36f),
           SkColorSetA(kBreaking, static_cast<U8CPU>(255 * alpha)), Align::kCenter);
  c->restore();
  if (age < 0.6f) {  // impact flash
    c->drawCircle(cx, cy, size * (0.5f + age * 1.5f),
                  FillA(kGold, (0.6f - age) * 0.8f));
  }
}

float Dashboard::DrawFeed(SkCanvas* c, const DashboardModel& m, float x, float y, float w,
                          float max_h) {
  if (feed_.empty() || max_h < 70 * s_) return 0;
  const float pad = 12 * s_;
  const float row = 22 * s_;
  const int rows = std::min<int>(static_cast<int>(feed_.size()),
                                 static_cast<int>((max_h - pad * 2 - 24 * s_) / row));
  if (rows <= 0) return 0;
  const float h = pad * 2 + 24 * s_ + rows * row;
  SkPaint panel = FillA(SkColorSetARGB(222, 10, 20, 33), 1);
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y, w, h), 12 * s_, 12 * s_), panel);
  float ty = y + pad + 14 * s_;
  DrawText(c, L_.T("feed.title"), x + pad, ty, fonts_->Bold(15 * s_), kText);
  // Blinking live dot.
  c->drawCircle(x + w - pad - 5 * s_, ty - 5 * s_, 4.5f * s_,
                FillA(kBreaking, 0.55f + 0.45f * std::sin(time_ * 5)));
  ty += 8 * s_;
  for (int i = 0; i < rows; ++i) {
    const ElectionEvent& e = feed_[i];
    ty += row;
    const bool breaking = election::IsBreaking(e.type);
    const float fresh = i == 0 ? Approach(flash_, "feed0", 0, 0.8f) : 0;
    (void)fresh;
    DrawText(c, e.time_label, x + pad, ty, fonts_->Regular(11.5f * s_), kText3);
    const float dot_x = x + pad + 42 * s_;
    c->drawCircle(dot_x, ty - 4.5f * s_, 4 * s_, FillA(EventColor(e.type), 1));
    const SkFont f = breaking ? fonts_->Bold(12.5f * s_) : fonts_->Regular(12.5f * s_);
    DrawText(c, Ellipsize(f, EventText(e, m), w - (dot_x - x) - pad - 8 * s_), dot_x + 9 * s_, ty,
             f, breaking ? kText : kText2);
  }
  return h;
}

void Dashboard::DrawTimeline(SkCanvas* c, const DashboardModel& m) {
  if (m.clock_minutes < 0 && !m.simulated) return;
  const float panel_x = Layout(m.width, m.height).panel_x;
  const float x0 = 16 * s_ + 34 * s_, x1 = panel_x - 110 * s_;
  const float track_y = m.height - 84 * s_;
  const float hist_h = 30 * s_;
  const double span = 420;  // 16:00 -> 23:00
  auto X = [&](double minute) {
    return x0 + static_cast<float>(std::clamp(minute / span, 0.0, 1.0)) * (x1 - x0);
  };
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(x0 - 44 * s_, track_y - hist_h - 16 * s_,
                                                     panel_x - 16 * s_, track_y + 20 * s_),
                                   10 * s_, 10 * s_),
               FillA(SkColorSetARGB(170, 10, 20, 33), 1));
  // Inflow histogram (votes per 5 minutes).
  int64_t peak = 1;
  for (int64_t v : inflow_) peak = std::max(peak, v);
  const float bw = (x1 - x0) / static_cast<float>(span / 5);
  for (size_t i = 0; i < inflow_.size(); ++i) {
    if (!inflow_[i]) continue;
    const float hh = hist_h * static_cast<float>(static_cast<double>(inflow_[i]) / peak);
    c->drawRect(SkRect::MakeXYWH(x0 + i * bw + 0.5f, track_y - 6 * s_ - hh, bw - 1, hh),
                FillA(SkColorSetRGB(88, 160, 230), 0.75f));
  }
  // Track + progress.
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(x0, track_y - 2 * s_, x1, track_y + 2 * s_),
                                   2 * s_, 2 * s_),
               FillA(SkColorSetARGB(70, 255, 255, 255), 1));
  const double now = std::max(0.0, m.clock_minutes);
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeLTRB(x0, track_y - 2 * s_, X(now), track_y + 2 * s_),
                                   2 * s_, 2 * s_),
               FillA(kGold, 1));
  for (int hr = 0; hr <= 7; ++hr) {
    const float tx = X(hr * 60.0);
    c->drawRect(SkRect::MakeXYWH(tx - 0.5f, track_y + 2 * s_, 1, 4 * s_), FillA(kText3, 1));
    char buf[8];
    std::snprintf(buf, sizeof buf, "%d:00", 16 + hr);
    DrawText(c, buf, tx, track_y + 15 * s_, fonts_->Regular(10 * s_), kText3, Align::kCenter);
  }
  // Event markers.
  for (const auto& [minute, type] : markers_) {
    if (type == EventType::kFirstReturns) continue;
    const float mx = X(minute);
    SkPathBuilder b;
    b.moveTo(mx, track_y - 4 * s_);
    b.lineTo(mx - 4 * s_, track_y - 10 * s_);
    b.lineTo(mx + 4 * s_, track_y - 10 * s_);
    b.close();
    c->drawPath(b.detach(), FillA(EventColor(type), 0.95f));
  }
  // Playhead.
  const float px = X(now);
  c->drawCircle(px, track_y, 6 * s_, FillA(SK_ColorWHITE, 1));
  c->drawCircle(px, track_y, 3.5f * s_, FillA(kGold, 1));
  const std::string clock = election::SimulatedResultsSource::ClockLabel(now);
  DrawText(c, clock, x0 - 40 * s_, track_y + 5 * s_, fonts_->Bold(13 * s_), kText);
  if (m.simulated) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "×%g", m.sim_speed);
    std::string speed = buf;
    if (m.sim_paused) speed = std::string(L_.T("timeline.paused")) + " " + speed;
    DrawText(c, speed, panel_x - 26 * s_, track_y + 5 * s_, fonts_->Bold(14 * s_),
             m.sim_paused ? kText3 : kGold, Align::kRight);
  }
}

void Dashboard::DrawNewsCounts(SkCanvas* c, const DashboardModel& m,
                               const std::string& candidate_id, float x, float y) {
  if (!m.news) return;
  auto it = m.news->by_candidate.find(candidate_id);
  if (it == m.news->by_candidate.end() || it->second.total() == 0) return;
  const store::SentimentCount& n = it->second;
  const SkFont f = fonts_->Bold(11.5f * s_);
  if (auto fl = flash_.find("news:" + candidate_id); fl != flash_.end() && fl->second > 0) {
    const float wbox = 90 * s_;
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x - 4 * s_, y - 12 * s_, wbox, 16 * s_), 8 * s_, 8 * s_),
                 FillA(kGold, 0.45f * fl->second));
  }
  x += DrawText(c, "▲" + std::to_string(n.good), x, y, f, SkColorSetRGB(46, 204, 113)) + 6 * s_;
  x += DrawText(c, "▼" + std::to_string(n.bad), x, y, f, SkColorSetRGB(231, 76, 60)) + 6 * s_;
  if (n.neutral) DrawText(c, "●" + std::to_string(n.neutral), x, y, fonts_->Regular(11 * s_), kText3);
}

void Dashboard::DrawNewsPanel(SkCanvas* c, const DashboardModel& m, float x, float y, float w,
                              float h) {
  if (h < 120 * s_) return;
  const float pad = 14 * s_;
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y, w, h), 12 * s_, 12 * s_),
               FillA(SkColorSetARGB(228, 10, 20, 33), 1));
  float ty = y + pad + 16 * s_;
  DrawText(c, L_.T("news.title"), x + pad, ty, fonts_->Bold(16 * s_), kText);
  const NewsView* nv = m.news;
  if (!nv || (!nv->enabled && nv->total.total() == 0)) {
    ty += 26 * s_;
    for (const std::string& line : {std::string(L_.T("news.disabled"))}) {
      DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), line, w - 2 * pad), x + pad, ty,
               fonts_->Regular(12.5f * s_), kText2);
    }
    return;
  }
  DrawText(c, Ellipsize(fonts_->Regular(10.5f * s_), nv->status, w * 0.5f), x + w - pad, ty,
           fonts_->Regular(10.5f * s_), kText3, Align::kRight);
  // Totals: good / neutral / bad bar.
  ty += 20 * s_;
  const store::SentimentCount& t = nv->total;
  DrawText(c,
           Fmt(L_.T("news.totals"), {std::to_string(t.good), std::to_string(t.bad),
                                     std::to_string(t.neutral)}),
           x + pad, ty, fonts_->Regular(12.5f * s_), kText2);
  ty += 8 * s_;
  const SkRect bar = SkRect::MakeXYWH(x + pad, ty, w - 2 * pad, 8 * s_);
  c->drawRRect(SkRRect::MakeRectXY(bar, 4 * s_, 4 * s_), FillA(SkColorSetARGB(60, 255, 255, 255), 1));
  if (t.total() > 0) {
    c->save();
    c->clipRRect(SkRRect::MakeRectXY(bar, 4 * s_, 4 * s_), true);
    const float g = bar.width() * t.good / t.total();
    const float n = bar.width() * t.neutral / t.total();
    c->drawRect(SkRect::MakeXYWH(bar.fLeft, bar.fTop, g, bar.height()), FillA(SkColorSetRGB(46, 204, 113), 1));
    c->drawRect(SkRect::MakeXYWH(bar.fLeft + g, bar.fTop, n, bar.height()), FillA(SkColorSetRGB(120, 130, 145), 1));
    c->drawRect(SkRect::MakeLTRB(bar.fLeft + g + n, bar.fTop, bar.fRight, bar.fBottom),
                FillA(SkColorSetRGB(231, 76, 60), 1));
    c->restore();
  }
  ty += 18 * s_;

  // Candidate leaderboard (current race, or most-covered candidates).
  struct Row {
    const election::Candidate* cand;
    store::SentimentCount n;
  };
  std::vector<Row> rows;
  const geo::Region& focus = m.tree->region(m.focus);
  for (const election::Race& race : m.data->races()) {
    if (focus.level != geo::Level::kNation && race.county_code != focus.county_code) continue;
    for (const election::Candidate& cand : race.candidates) {
      auto it = nv->by_candidate.find(cand.id);
      if (it != nv->by_candidate.end() && it->second.total() > 0) rows.push_back({&cand, it->second});
    }
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    return a.n.total() != b.n.total() ? a.n.total() > b.n.total()
                                      : a.n.good - a.n.bad > b.n.good - b.n.bad;
  });
  const size_t max_rows = std::min<size_t>(rows.size(), focus.level == geo::Level::kNation ? 6 : 7);
  int peak = 1;
  for (size_t i = 0; i < max_rows; ++i) peak = std::max(peak, std::max(rows[i].n.good, rows[i].n.bad));
  const float row_h = 24 * s_;
  const float mid = x + pad + 150 * s_;
  const float half = (x + w - pad - mid) / 2;
  for (size_t i = 0; i < max_rows; ++i) {
    const Row& row = rows[i];
    ty += row_h;
    const election::Party& p = m.data->party(row.cand->party);
    avatars_->Draw(c, *row.cand, p, x + pad, ty - 15 * s_, 20 * s_);
    DrawText(c, Ellipsize(fonts_->Bold(12.5f * s_), L_.CandidateName(*row.cand), 110 * s_),
             x + pad + 26 * s_, ty, fonts_->Bold(12.5f * s_), kText);
    // Diverging bars: bad to the left of the axis, good to the right.
    const float gb = half * row.n.good / peak, bb = half * row.n.bad / peak;
    const float axis = mid + half;
    c->drawRect(SkRect::MakeXYWH(axis - bb, ty - 10 * s_, bb, 8 * s_), FillA(SkColorSetRGB(231, 76, 60), 1));
    c->drawRect(SkRect::MakeXYWH(axis, ty - 10 * s_, gb, 8 * s_), FillA(SkColorSetRGB(46, 204, 113), 1));
    c->drawRect(SkRect::MakeXYWH(axis - 0.5f, ty - 13 * s_, 1, 14 * s_), FillA(kText3, 1));
    DrawText(c, std::to_string(row.n.bad), axis - bb - 4 * s_, ty - 1 * s_, fonts_->Regular(10.5f * s_),
             SkColorSetRGB(231, 76, 60), Align::kRight);
    DrawText(c, std::to_string(row.n.good), axis + gb + 4 * s_, ty - 1 * s_, fonts_->Regular(10.5f * s_),
             SkColorSetRGB(46, 204, 113));
  }
  ty += 14 * s_;
  // Latest headlines with per-candidate verdicts.
  for (const store::ClassifiedArticle& a : nv->latest) {
    if (ty + 40 * s_ > y + h - pad) break;
    ty += 20 * s_;
    std::string title = a.digest.empty() ? a.article.title : a.digest;
    if (a.article.simulated && title.rfind("【模擬】", 0) != 0) title = "【模擬】" + title;
    DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), title, w - 2 * pad), x + pad, ty,
             fonts_->Regular(12.5f * s_), kText);
    ty += 17 * s_;
    float cx = x + pad;
    const std::string src = a.article.source + " · " + a.model;
    cx += DrawText(c, Ellipsize(fonts_->Regular(10.5f * s_), src, 120 * s_), cx, ty,
                   fonts_->Regular(10.5f * s_), kText3) + 8 * s_;
    for (const store::Assessment& as : a.assessments) {
      const election::Race* race = nullptr;
      const election::Candidate* cand = m.data->CandidateById(as.candidate_id, &race);
      if (!cand) continue;
      const uint32_t color = as.sentiment == store::Sentiment::kGood ? 0xFF2ECC71
                             : as.sentiment == store::Sentiment::kBad ? 0xFFE74C3C
                                                                       : 0xFF7F8C8D;
      const std::string label =
          L_.CandidateName(*cand) + (as.sentiment == store::Sentiment::kGood  ? " ▲"
                                     : as.sentiment == store::Sentiment::kBad ? " ▼"
                                                                              : " ●");
      const float cw = TextWidth(fonts_->Bold(10.5f * s_), label) + 10 * s_;
      if (cx + cw > x + w - pad) break;
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(cx, ty - 11 * s_, cw, 15 * s_), 7 * s_, 7 * s_),
                   FillA(color, 0.85f));
      DrawText(c, label, cx + 5 * s_, ty, fonts_->Bold(10.5f * s_), SK_ColorWHITE);
      cx += cw + 4 * s_;
    }
  }
}

void Dashboard::DrawNotice(SkCanvas* c, const DashboardModel& m) {
  notice_age_ += dt_;
  if (notice_.empty() || notice_age_ > 2.5f) return;
  const float a = Envelope(notice_age_, 2.5f, 0.15f, 0.6f);
  const SkFont f = fonts_->Bold(14 * s_);
  const float w = TextWidth(f, notice_) + 36 * s_;
  const float panel_x = Layout(m.width, m.height).panel_x;
  const float x = (panel_x - w) / 2 + 170 * s_, y = m.height - 150 * s_;
  c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x, y, w, 32 * s_), 16 * s_, 16 * s_),
               FillA(SkColorSetARGB(235, 30, 45, 64), a));
  DrawText(c, "★", x + 10 * s_, y + 21 * s_, f, SkColorSetA(kGold, static_cast<U8CPU>(255 * a)));
  DrawText(c, notice_, x + 26 * s_, y + 21 * s_, f, SkColorSetA(kText, static_cast<U8CPU>(255 * a)));
}

}  // namespace twn::ui
