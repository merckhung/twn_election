#include "src/ui/dashboard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>

#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"

namespace twn::ui {

using election::Candidate;
using election::Party;
using election::Race;
using election::RefTally;
using election::ResultsStatus;
using election::Tally;

namespace {

constexpr SkColor kPanel = SkColorSetARGB(222, 10, 20, 33);
constexpr SkColor kPanelBorder = SkColorSetARGB(40, 255, 255, 255);
constexpr SkColor kText = SkColorSetRGB(242, 245, 248);
constexpr SkColor kText2 = SkColorSetRGB(160, 176, 195);
constexpr SkColor kText3 = SkColorSetRGB(110, 126, 146);
constexpr SkColor kAccent = SkColorSetRGB(245, 197, 66);
constexpr SkColor kTrack = SkColorSetARGB(60, 255, 255, 255);
constexpr uint32_t kNeutral = 0xFF2B3D52;
constexpr uint32_t kAgree = 0xFF3FA7D6;
constexpr uint32_t kDisagree = 0xFFE4572E;

SkPaint Fill(SkColor c) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor(c);
  return p;
}

void Panel(SkCanvas* c, const SkRect& r, float radius) {
  c->drawRRect(SkRRect::MakeRectXY(r, radius, radius), Fill(kPanel));
  SkPaint border = Fill(kPanelBorder);
  border.setStyle(SkPaint::kStroke_Style);
  border.setStrokeWidth(1);
  c->drawRRect(SkRRect::MakeRectXY(r.makeInset(0.5f, 0.5f), radius, radius), border);
}

void Bar(SkCanvas* c, const SkRect& r, float fraction, SkColor color, SkColor track = kTrack) {
  const float rad = r.height() * 0.5f;
  c->drawRRect(SkRRect::MakeRectXY(r, rad, rad), Fill(track));
  fraction = std::clamp(fraction, 0.f, 1.f);
  if (fraction <= 0) return;
  SkRect f = r;
  f.fRight = r.fLeft + std::max(r.height(), r.width() * fraction);
  c->drawRRect(SkRRect::MakeRectXY(f, rad, rad), Fill(color));
}

std::string Percent(double v, int decimals = 1) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.*f%%", decimals, v * 100.0);
  return buf;
}

// Splits UTF-8 text into lines no wider than `width`. CJK characters may
// break anywhere; runs of Latin letters/digits break at spaces.
std::vector<std::string> Wrap(const SkFont& font, const std::string& text, float width) {
  std::vector<std::string> tokens;
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = text[i];
    if (c < 0x80 && c != ' ') {
      size_t j = i;
      while (j < text.size() && static_cast<unsigned char>(text[j]) < 0x80 && text[j] != ' ') ++j;
      tokens.push_back(text.substr(i, j - i));
      i = j;
    } else {
      const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
      tokens.push_back(text.substr(i, n));
      i += n;
    }
  }
  std::vector<std::string> lines;
  std::string line;
  for (const std::string& tok : tokens) {
    if (!line.empty() && TextWidth(font, line + tok) > width) {
      while (!line.empty() && line.back() == ' ') line.pop_back();
      lines.push_back(line);
      line.clear();
      if (tok == " ") continue;
    }
    line += tok;
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

float ChipWidth(const Fonts* fonts, const std::string& text, float h) {
  return TextWidth(fonts->Bold(h * 0.62f), text) + h * 0.8f;
}

// Draws a small rounded "chip" with text; returns its width.
float Chip(SkCanvas* c, const Fonts* fonts, const std::string& text, float x, float y, float h,
           uint32_t color, float s, bool outline = false) {
  const SkFont f = fonts->Bold(h * 0.62f);
  const float w = TextWidth(f, text) + h * 0.8f;
  const SkRect r = SkRect::MakeXYWH(x, y, w, h);
  if (outline) {
    SkPaint p = Fill(color);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1.2f * s);
    c->drawRRect(SkRRect::MakeRectXY(r.makeInset(0.6f * s, 0.6f * s), h / 2, h / 2), p);
    DrawText(c, text, x + w / 2, y + h * 0.71f, f, color, Align::kCenter);
  } else {
    c->drawRRect(SkRRect::MakeRectXY(r, h / 2, h / 2), Fill(color));
    const SkColor tc = Luminance(color) > 0.6f ? SK_ColorBLACK : SK_ColorWHITE;
    DrawText(c, text, x + w / 2, y + h * 0.71f, f, tc, Align::kCenter);
  }
  return w;
}

struct Countdown {
  int days = 0;
  bool voting = false;
  bool after_polls = false;
};

Countdown ComputeCountdown(const std::string& date, std::chrono::system_clock::time_point now) {
  Countdown cd;
  int y = 0, mo = 0, d = 0;
  if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &mo, &d) != 3) return cd;
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  // 08:00 Asia/Taipei == 00:00 UTC.
  const std::time_t open_utc = timegm(&tm);
  const std::time_t close_utc = open_utc + 8 * 3600;
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  if (t < open_utc) {
    cd.days = static_cast<int>((open_utc - t + 86399) / 86400);
  } else if (t < close_utc) {
    cd.voting = true;
  } else {
    cd.after_polls = true;
  }
  return cd;
}

const Race* RaceFor(const DashboardModel& m, int region) {
  return region >= 0 ? m.results->RaceForRegion(region) : nullptr;
}

bool HasVotes(const Tally& t) { return t.TotalVotes() > 0; }

}  // namespace

const char* ColorModeName(Lang lang, ColorMode m) {
  switch (m) {
    case ColorMode::kLeader: return Tr(lang, "mode.leader");
    case ColorMode::kProgress: return Tr(lang, "mode.progress");
    case ColorMode::kTurnout: return Tr(lang, "mode.turnout");
    case ColorMode::kReferendum: return Tr(lang, "mode.referendum");
    default: return "";
  }
}

uint32_t MixColor(uint32_t a, uint32_t b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  auto ch = [&](int shift) {
    const float x = ((a >> shift) & 0xFF) * (1 - t) + ((b >> shift) & 0xFF) * t;
    return static_cast<uint32_t>(std::lround(x)) << shift;
  };
  return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

float Luminance(uint32_t c) {
  return (0.299f * ((c >> 16) & 0xFF) + 0.587f * ((c >> 8) & 0xFF) + 0.114f * (c & 0xFF)) / 255.f;
}

DashboardLayout Dashboard::Layout(int width, int height) {
  DashboardLayout l;
  l.scale = std::clamp(std::min(width / 1600.f, height / 900.f), 0.7f, 2.5f);
  l.panel_x = width - (430 + 16) * l.scale;
  return l;
}

uint32_t Dashboard::RegionColor(const DashboardModel& m, int region, float* strength) {
  *strength = 0;
  const geo::Region& r = m.tree->region(region);
  if (m.mode == ColorMode::kReferendum) {
    const auto& refs = m.data->info().referendums;
    if (refs.empty()) return kNeutral;
    const RefTally& t = m.results->ReferendumTally(refs[0].id, region);
    const int64_t total = t.agree + t.disagree;
    if (total <= 0) return kNeutral;
    const float agree = static_cast<float>(t.agree) / static_cast<float>(total);
    *strength = std::clamp(0.35f + std::fabs(agree - 0.5f) * 3.f, 0.f, 1.f);
    return agree >= 0.5f ? kAgree : kDisagree;
  }
  const Race* race = RaceFor(m, region);
  if (!race) return kNeutral;
  const Tally& t = m.results->RaceTally(*race, region);
  switch (m.mode) {
    case ColorMode::kProgress:
      if (t.units_total <= 0) return kNeutral;
      *strength = static_cast<float>(t.Progress());
      return 0xFFF5A623;
    case ColorMode::kTurnout: {
      if (t.eligible <= 0 || t.ballots_cast <= 0) return kNeutral;
      *strength = std::clamp(static_cast<float>((t.Turnout() - 0.4) / 0.4), 0.05f, 1.f);
      return 0xFF2EC4B6;
    }
    default: {
      if (HasVotes(t)) {
        const std::vector<int> rank = t.Ranking();
        const double margin =
            t.Share(rank[0]) - (rank.size() > 1 ? t.Share(rank[1]) : 0.0);
        *strength = std::clamp(0.45f + static_cast<float>(margin) * 2.5f, 0.45f, 1.f);
        return m.data->party(race->candidates[rank[0]].party).color;
      }
      // Before counting: tint by the incumbent's party.
      *strength = r.level == geo::Level::kCounty ? 0.42f : 0.3f;
      return m.data->party(race->incumbent.party).color;
    }
  }
}

void Dashboard::Render(const DashboardModel& m, uint8_t* pixels) {
  const SkImageInfo info = SkImageInfo::MakeN32Premul(m.width, m.height);
  std::unique_ptr<SkCanvas> canvas =
      SkCanvas::MakeRasterDirect(info, pixels, static_cast<size_t>(m.width) * 4);
  SkCanvas* c = canvas.get();
  c->clear(SK_ColorTRANSPARENT);
  L_ = Localizer(m.lang);
  fonts_->SetJapanese(m.lang == Lang::kJa);
  const DashboardLayout layout = Layout(m.width, m.height);
  s_ = layout.scale;

  DrawInsets(c, m);
  DrawLabels(c, m);
  DrawHeader(c, m);
  DrawLeftColumn(c, m);
  const float margin = 16 * s_;
  const SkRect panel = SkRect::MakeLTRB(layout.panel_x, margin, m.width - margin,
                                        m.height - margin - 26 * s_);
  if (m.tree->region(m.focus).level == geo::Level::kNation) {
    DrawNationPanel(c, m, panel);
  } else {
    DrawRacePanel(c, m, panel);
  }
  DrawFooter(c, m);
  DrawTooltip(c, m);
  if (m.show_help) DrawHelp(c, m);
}

void Dashboard::DrawHeader(SkCanvas* c, const DashboardModel& m) {
  const float x = 18 * s_;
  float y = 44 * s_;
  const auto& info = m.data->info();
  const std::string title = L_.T("app.title");
  DrawText(c, title, x, y, fonts_->Bold(28 * s_), kText);
  float cx = x + TextWidth(fonts_->Bold(28 * s_), title) + 12 * s_;

  // Status chip.
  const auto& snap = m.results->snapshot();
  std::string status;
  uint32_t status_color;
  if (snap.simulated) {
    status = L_.T("status.simulation");
    status_color = 0xFFD64545;
  } else if (snap.status == ResultsStatus::kPreElection) {
    const Countdown cd = ComputeCountdown(info.date, m.now);
    status = cd.voting ? Fmt(L_.T("status.voting"), {info.polls_open, info.polls_close})
             : cd.after_polls ? std::string(L_.T("status.awaiting"))
                              : Fmt(L_.T("status.countdown"), {std::to_string(cd.days)});
    status_color = 0xFF3A6EA5;
  } else if (snap.status == ResultsStatus::kCounting) {
    status = L_.T("status.counting");
    status_color = 0xFFE08E0B;
  } else {
    status = L_.T("status.final");
    status_color = 0xFF2E9E5B;
  }
  Chip(c, fonts_, status, cx, y - 22 * s_, 26 * s_, status_color, s_);

  y += 24 * s_;
  DrawText(c, Fmt(L_.T("header.subtitle"), {info.date, info.polls_open, info.polls_close}), x, y,
           fonts_->Regular(14 * s_), kText2);

  // Breadcrumb.
  y += 30 * s_;
  const std::vector<int> path = m.tree->Path(m.focus);
  float bx = x;
  for (size_t i = 0; i < path.size(); ++i) {
    const bool last = i + 1 == path.size();
    const SkFont f = last ? fonts_->Bold(19 * s_) : fonts_->Regular(19 * s_);
    bx += DrawText(c, L_.RegionName(m.tree->region(path[i])), bx, y, f, last ? kAccent : kText2);
    if (!last) bx += DrawText(c, "  ›  ", bx, y, fonts_->Regular(19 * s_), kText3);
  }
  const geo::Region& focus = m.tree->region(m.focus);
  const std::string alt = m.lang == Lang::kEn ? focus.name_zh : focus.name_en;
  if (!alt.empty()) DrawText(c, alt, x, y + 20 * s_, fonts_->Regular(12.5f * s_), kText3);
}

void Dashboard::DrawLeftColumn(SkCanvas* c, const DashboardModel& m) {
  const float x = 16 * s_;
  const float w = 330 * s_;
  float y = 150 * s_;
  const float pad = 14 * s_;
  const geo::Region& focus = m.tree->region(m.focus);
  const auto& info = m.data->info();

  if (focus.level == geo::Level::kNation) {
    // Offices on the ballot the same day.
    const float row = 21 * s_;
    const float h = pad * 2 + 30 * s_ + row * info.offices.size() + 24 * s_;
    Panel(c, SkRect::MakeXYWH(x, y, w, h), 12 * s_);
    float ty = y + pad + 16 * s_;
    DrawText(c, L_.T("offices.title"), x + pad, ty, fonts_->Bold(16 * s_), kText);
    ty += 22 * s_;
    DrawText(c, L_.T("offices.seats"), x + w - pad - 70 * s_, ty - 2 * s_,
             fonts_->Regular(11 * s_), kText3, Align::kRight);
    DrawText(c, L_.T("offices.registered"), x + w - pad, ty - 2 * s_, fonts_->Regular(11 * s_),
             kText3, Align::kRight);
    for (const auto& o : info.offices) {
      ty += row;
      const std::string name = L_.OfficeName(o);
      const bool mayor = o.headline;
      DrawText(c, Ellipsize(fonts_->Regular(13.5f * s_), name, w - 130 * s_), x + pad, ty,
               mayor ? fonts_->Bold(13.5f * s_) : fonts_->Regular(13.5f * s_),
               mayor ? kAccent : kText2);
      DrawText(c, FormatThousands(o.seats), x + w - pad - 70 * s_, ty, fonts_->Regular(13.5f * s_),
               kText, Align::kRight);
      DrawText(c, FormatThousands(o.candidates), x + w - pad, ty, fonts_->Regular(13.5f * s_),
               kText2, Align::kRight);
    }
    ty += row + 2 * s_;
    DrawText(c,
             Fmt(L_.T("offices.total"),
                 {FormatThousands(info.total_seats), FormatThousands(info.total_candidates)}),
             x + pad, ty, fonts_->Regular(12 * s_), kText3);
    y += h + 12 * s_;
  } else {
    // Incumbent + region facts.
    const Race* race = m.data->RaceForCounty(focus.county_code);
    const float h = 120 * s_;
    Panel(c, SkRect::MakeXYWH(x, y, w, h), 12 * s_);
    float ty = y + pad + 16 * s_;
    DrawText(c, L_.T("area.title"), x + pad, ty, fonts_->Bold(16 * s_), kText);
    ty += 26 * s_;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", focus.area_km2);
    std::string facts = Fmt(L_.T("area.size"), {buf});
    if (!focus.children.empty()) {
      facts += " · " + Fmt(L_.T(focus.level == geo::Level::kCounty ? "area.towns" : "area.villages"),
                           {std::to_string(focus.children.size())});
    }
    DrawText(c, facts, x + pad, ty, fonts_->Regular(13.5f * s_), kText2);
    if (race) {
      ty += 28 * s_;
      const Party& p = m.data->party(race->incumbent.party);
      float cx = x + pad;
      cx += DrawText(c, std::string(L_.T("area.incumbent")) + " ", cx, ty,
                     fonts_->Regular(13.5f * s_), kText2);
      std::string inc_name = m.lang == Lang::kJa ? ToJapaneseKanji(race->incumbent.name_zh)
                                                 : race->incumbent.name_zh;
      for (const Candidate& cand : race->candidates) {
        if (cand.name_zh == race->incumbent.name_zh) inc_name = L_.CandidateName(cand);
      }
      cx += DrawText(c, inc_name, cx, ty, fonts_->Bold(15 * s_), kText) + 8 * s_;
      cx += Chip(c, fonts_, L_.PartyShort(p), cx, ty - 15 * s_, 20 * s_, p.color, s_) + 6 * s_;
      if (race->incumbent.term_limited) {
        Chip(c, fonts_, L_.T("area.term_limited"), cx, ty - 15 * s_, 20 * s_, 0xFF9FB0C3, s_, true);
      }
      ty += 22 * s_;
      // Notes in the data file are English research notes.
      if (m.lang == Lang::kEn && !race->incumbent.note.empty()) {
        DrawText(c, Ellipsize(fonts_->Regular(12 * s_), race->incumbent.note, w - 2 * pad),
                 x + pad, ty, fonts_->Regular(12 * s_), kText3);
      }
    }
    y += h + 12 * s_;
  }

  // Referendum card.
  if (!info.referendums.empty()) {
    const auto& ref = info.referendums[0];
    const SkFont qf = fonts_->Regular(13.5f * s_);
    const std::vector<std::string> lines = Wrap(qf, L_.Question(ref), w - 2 * pad);
    const float h = pad * 2 + 24 * s_ + lines.size() * 19 * s_ + 70 * s_;
    Panel(c, SkRect::MakeXYWH(x, y, w, h), 12 * s_);
    float ty = y + pad + 16 * s_;
    DrawText(c, Fmt(L_.T("ref.title"), {std::to_string(ref.case_no)}), x + pad, ty,
             fonts_->Bold(16 * s_), kText);
    for (const std::string& line : lines) {
      ty += 19 * s_;
      DrawText(c, line, x + pad, ty + 4 * s_, qf, kText2);
    }
    ty += 30 * s_;
    const RefTally& t = m.results->ReferendumTally(ref.id, m.focus);
    const int64_t total = t.agree + t.disagree;
    const float agree = total > 0 ? static_cast<float>(t.agree) / total : 0.f;
    const SkRect bar = SkRect::MakeXYWH(x + pad, ty, w - 2 * pad, 12 * s_);
    c->drawRRect(SkRRect::MakeRectXY(bar, 6 * s_, 6 * s_), Fill(kTrack));
    if (total > 0) {
      c->save();
      c->clipRRect(SkRRect::MakeRectXY(bar, 6 * s_, 6 * s_), true);
      c->drawRect(SkRect::MakeLTRB(bar.fLeft, bar.fTop, bar.fLeft + bar.width() * agree,
                                   bar.fBottom),
                  Fill(kAgree));
      c->drawRect(SkRect::MakeLTRB(bar.fLeft + bar.width() * agree, bar.fTop, bar.fRight,
                                   bar.fBottom),
                  Fill(kDisagree));
      c->restore();
      // 25%-of-electorate threshold marker (national level only is meaningful).
      if (t.eligible > 0) {
        const float th = std::clamp(0.25f * t.eligible / static_cast<float>(total), 0.f, 1.f);
        SkPaint p = Fill(SK_ColorWHITE);
        p.setStrokeWidth(2 * s_);
        c->drawLine(bar.fLeft + bar.width() * th, bar.fTop - 3 * s_,
                    bar.fLeft + bar.width() * th, bar.fBottom + 3 * s_, p);
      }
    }
    ty += 32 * s_;
    DrawText(c,
             std::string(L_.T("ref.agree")) + " " +
                 (total ? Percent(agree) + " · " + FormatThousands(t.agree) : "—"),
             x + pad, ty, fonts_->Bold(13.5f * s_), kAgree);
    DrawText(c, std::string(L_.T("ref.disagree")) + " " + (total ? Percent(1 - agree) : "—"),
             x + w - pad, ty, fonts_->Bold(13.5f * s_), kDisagree, Align::kRight);
    ty += 18 * s_;
    DrawText(c,
             Ellipsize(fonts_->Regular(11.5f * s_),
                       t.units_total ? Fmt(L_.T("ref.threshold_short"), {Percent(t.Progress(), 0)})
                                     : std::string(L_.T("ref.threshold")),
                       w - 2 * pad),
             x + pad, ty, fonts_->Regular(11.5f * s_), kText3);
  }
}

void Dashboard::DrawNationPanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 20 * s_;
  const auto& races = m.data->races();
  int total_candidates = 0;
  for (const Race& r : races) total_candidates += static_cast<int>(r.candidates.size());

  DrawText(c, Fmt(L_.T("nation.title"), {std::to_string(races.size())}), x, y,
           fonts_->Bold(22 * s_), kText);
  DrawText(c, Fmt(L_.T("nation.candidates"), {std::to_string(total_candidates)}), x + w, y,
           fonts_->Regular(13 * s_), kText2, Align::kRight);
  y += 20 * s_;

  // Seats by leading party (or incumbent before counting).
  bool any_votes = false;
  std::map<std::string, int> seats;
  std::vector<std::pair<const Race*, int>> leaders;  // race, leading candidate or -1
  for (const Race& r : races) {
    const int county = m.tree->FindByCode(r.county_code);
    const Tally& t = county >= 0 ? m.results->RaceTally(r, county) : Tally{};
    const int lead = HasVotes(t) ? t.Leader() : -1;
    any_votes |= lead >= 0;
    leaders.push_back({&r, lead});
  }
  for (const auto& [r, lead] : leaders) {
    seats[lead >= 0 ? r->candidates[lead].party : (any_votes ? "" : r->incumbent.party)]++;
  }
  const bool final = m.results->snapshot().status == ResultsStatus::kFinal;
  DrawText(c,
           L_.T(any_votes ? (final ? "nation.seats_final" : "nation.seats_leading")
                          : "nation.seats_incumbent"),
           x, y + 4 * s_, fonts_->Regular(12 * s_), kText3);
  y += 12 * s_;
  const SkRect seat_bar = SkRect::MakeXYWH(x, y, w, 16 * s_);
  c->drawRRect(SkRRect::MakeRectXY(seat_bar, 4 * s_, 4 * s_), Fill(kTrack));
  std::vector<std::pair<std::string, int>> ordered(seats.begin(), seats.end());
  std::sort(ordered.begin(), ordered.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  float sx = x;
  const float seg = w / races.size();
  for (const auto& [party, n] : ordered) {
    if (party.empty()) continue;
    for (int i = 0; i < n; ++i) {
      c->drawRect(SkRect::MakeXYWH(sx + 0.5f, y, seg - 1, 16 * s_), Fill(m.data->party(party).color));
      sx += seg;
    }
  }
  SkPaint mid = Fill(SK_ColorWHITE);
  mid.setStrokeWidth(1.5f * s_);
  c->drawLine(x + w / 2, y - 3 * s_, x + w / 2, y + 19 * s_, mid);
  y += 30 * s_;
  float cx = x;
  for (const auto& [party, n] : ordered) {
    if (party.empty()) continue;
    const Party& p = m.data->party(party);
    cx += Chip(c, fonts_, L_.PartyShort(p) + " " + std::to_string(n), cx, y - 14 * s_, 20 * s_,
               p.color, s_) + 6 * s_;
  }
  y += 16 * s_;

  // Race list.
  const float row_h = std::max(22 * s_, (panel.fBottom - pad - y) / races.size());
  const float avatar = std::min(row_h - 4 * s_, 24 * s_);
  int hover_county = m.hover >= 0 ? m.tree->AncestorAt(m.hover, geo::Level::kCounty) : -1;
  for (const auto& [race, lead] : leaders) {
    const int county = m.tree->FindByCode(race->county_code);
    const float mid_y = y + row_h / 2;
    if (county >= 0 && county == hover_county) {
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(x - 6 * s_, y, w + 12 * s_, row_h),
                                       6 * s_, 6 * s_),
                   Fill(SkColorSetARGB(40, 255, 255, 255)));
    }
    const Party& inc = m.data->party(race->incumbent.party);
    c->drawRect(SkRect::MakeXYWH(x - 2 * s_, mid_y - avatar * 0.4f, 3 * s_, avatar * 0.8f),
                Fill(lead >= 0 ? m.data->party(race->candidates[lead].party).color : inc.color));
    std::string county_name = county >= 0 ? L_.RegionName(m.tree->region(county))
                                          : race->county_zh;
    if (m.lang == Lang::kEn) {
      // "Taipei City" -> "Taipei": the panel title already says what they are.
      for (const char* suffix : {" County", " City"}) {
        const size_t pos = county_name.rfind(suffix);
        if (pos != std::string::npos && pos + std::strlen(suffix) == county_name.size()) {
          county_name.resize(pos);
        }
      }
    }
    const float name_w = m.lang == Lang::kEn ? 92 * s_ : 70 * s_;
    DrawText(c, Ellipsize(fonts_->Bold(14 * s_), county_name, name_w - 6 * s_), x + 6 * s_,
             mid_y + 5 * s_, fonts_->Bold(14 * s_), kText);
    const float list_x = x + name_w + 4 * s_;
    if (lead < 0) {
      // Before counting: the full set of candidates.
      float ax = list_x;
      const float step =
          std::min(avatar + 3 * s_, (x + w - list_x - 30 * s_) / race->candidates.size());
      for (const Candidate& cand : race->candidates) {
        avatars_->Draw(c, cand, m.data->party(cand.party), ax, mid_y - avatar / 2, avatar);
        ax += step;
      }
      if (race->candidates.size() <= 3) {
        std::string names;
        for (const Candidate& cand : race->candidates) {
          names += (names.empty() ? "" : L_.ListSeparator()) + L_.CandidateName(cand);
        }
        DrawText(c, Ellipsize(fonts_->Regular(12.5f * s_), names, x + w - ax - 34 * s_), ax + 4 * s_,
                 mid_y + 4.5f * s_, fonts_->Regular(12.5f * s_), kText2);
      }
      DrawText(c, Fmt(L_.T("nation.count"), {std::to_string(race->candidates.size())}), x + w,
               mid_y + 4.5f * s_, fonts_->Regular(12 * s_), kText3, Align::kRight);
    } else {
      const Candidate& cand = race->candidates[lead];
      const Party& p = m.data->party(cand.party);
      const Tally& t = m.results->RaceTally(*race, county);
      avatars_->Draw(c, cand, p, list_x, mid_y - avatar / 2, avatar);
      DrawText(c, Ellipsize(fonts_->Bold(13.5f * s_), L_.CandidateName(cand), 70 * s_),
               list_x + avatar + 6 * s_, mid_y + 5 * s_, fonts_->Bold(13.5f * s_), kText);
      const float bx = list_x + avatar + 80 * s_;
      const float bw = w - (bx - x) - 58 * s_;
      Bar(c, SkRect::MakeXYWH(bx, mid_y - 4 * s_, bw, 8 * s_), static_cast<float>(t.Share(lead)),
          p.color);
      DrawText(c, Percent(t.Share(lead)), x + w, mid_y + 5 * s_, fonts_->Bold(13 * s_), kText,
               Align::kRight);
      // Counting progress as a thin line under the row.
      Bar(c, SkRect::MakeXYWH(bx, mid_y + 7 * s_, bw, 2 * s_), static_cast<float>(t.Progress()),
          kAccent, SkColorSetARGB(25, 255, 255, 255));
    }
    y += row_h;
  }
}

void Dashboard::DrawRacePanel(SkCanvas* c, const DashboardModel& m, SkRect panel) {
  Panel(c, panel, 14 * s_);
  const geo::Region& focus = m.tree->region(m.focus);
  const Race* race = m.data->RaceForCounty(focus.county_code);
  const float pad = 16 * s_;
  const float x = panel.fLeft + pad;
  const float w = panel.width() - 2 * pad;
  float y = panel.fTop + pad + 22 * s_;
  if (!race) {
    DrawText(c, L_.T("race.none"), x, y, fonts_->Bold(18 * s_), kText);
    return;
  }
  const float chip_w = race->municipal ? TextWidth(fonts_->Bold(12.4f * s_), L_.T("race.municipal")) + 16 * s_ : 0;
  const SkFont title_font = fonts_->Bold(m.lang == Lang::kEn ? 21 * s_ : 24 * s_);
  DrawText(c, Ellipsize(title_font, Fmt(L_.T("race.title"), {L_.RaceTitle(*race)}), w - chip_w - 8 * s_),
           x, y, title_font, kText);
  if (race->municipal) {
    Chip(c, fonts_, L_.T("race.municipal"), x + w - chip_w, y - 18 * s_, 20 * s_, 0xFF9FB0C3, s_,
         true);
  }
  y += 22 * s_;
  const bool city = race->county_zh.find("市") != std::string::npos;
  const std::string scope =
      focus.level == geo::Level::kCounty
          ? std::string(L_.T(city ? "race.scope_city" : "race.scope_county"))
          : Fmt(L_.T("race.scope_region"), {L_.RegionName(focus)});
  DrawText(c, scope, x, y, fonts_->Regular(14 * s_), kAccent);
  y += 18 * s_;

  const Tally& t = m.results->RaceTally(*race, m.focus);
  const bool counting = HasVotes(t);
  // Progress + turnout.
  Bar(c, SkRect::MakeXYWH(x, y, w, 6 * s_), static_cast<float>(t.Progress()), kAccent);
  y += 22 * s_;
  std::string progress =
      t.units_total > 0 ? Fmt(L_.T("race.progress"), {std::to_string(t.units_counted),
                                                        std::to_string(t.units_total),
                                                        Percent(t.Progress(), 0)})
                        : std::string(L_.T("race.not_started"));
  DrawText(c, progress, x, y, fonts_->Regular(13 * s_), kText2);
  if (t.ballots_cast > 0 && t.eligible > 0) {
    DrawText(c, Fmt(L_.T("race.turnout"), {Percent(t.Turnout())}), x + w, y,
             fonts_->Regular(13 * s_), kText2, Align::kRight);
  }
  y += 14 * s_;

  // Candidate cards.
  std::vector<int> order(race->candidates.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
  if (counting) {
    order = t.Ranking();
  } else {
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      const auto& ca = race->candidates[a];
      const auto& cb = race->candidates[b];
      return ca.ballot_no.value_or(100 + a) < cb.ballot_no.value_or(100 + b);
    });
  }
  const float avail = panel.fBottom - pad - 40 * s_ - y;
  const float card_h = std::min(92 * s_, avail / std::max<size_t>(1, order.size()));
  const float photo = std::min(card_h - 16 * s_, 62 * s_);
  const bool final = m.results->snapshot().status == ResultsStatus::kFinal;
  for (size_t rank = 0; rank < order.size(); ++rank) {
    const int i = order[rank];
    const Candidate& cand = race->candidates[i];
    const Party& p = m.data->party(cand.party);
    const SkRect card = SkRect::MakeXYWH(x - 6 * s_, y + 4 * s_, w + 12 * s_, card_h - 6 * s_);
    const bool leader = counting && rank == 0;
    c->drawRRect(SkRRect::MakeRectXY(card, 10 * s_, 10 * s_),
                 Fill(leader ? SkColorSetARGB(46, 245, 197, 66) : SkColorSetARGB(18, 255, 255, 255)));
    const float cy = card.fTop + (card.height() - photo) / 2;
    avatars_->Draw(c, cand, p, x + 2 * s_, cy, photo);
    // Ballot number badge.
    const std::string no = cand.ballot_no ? std::to_string(*cand.ballot_no) : "?";
    const float br = 10 * s_;
    c->drawCircle(x + 2 * s_ + photo - br * 0.6f, cy + photo - br * 0.6f, br, Fill(SK_ColorWHITE));
    DrawText(c, no, x + 2 * s_ + photo - br * 0.6f, cy + photo - br * 0.6f + 4.5f * s_,
             fonts_->Bold(13 * s_), SK_ColorBLACK, Align::kCenter);

    const float tx = x + photo + 14 * s_;
    const float name_y = card.fTop + card.height() * 0.36f;
    float nx = tx;
    const SkFont name_font = fonts_->Bold(std::min(m.lang == Lang::kEn ? 17 * s_ : 20 * s_,
                                                   card_h * 0.24f));
    const int64_t votes = i < static_cast<int>(t.votes.size()) ? t.votes[i] : 0;
    const std::string votes_text = counting ? FormatThousands(votes) : "0";
    // Name and badges must stay clear of the vote count on the right.
    const float limit = x + w - TextWidth(fonts_->Bold(19 * s_), votes_text) - 10 * s_;
    nx += DrawText(c, Ellipsize(name_font, L_.CandidateName(cand), limit - nx - 50 * s_), nx,
                   name_y, name_font, kText) + 8 * s_;
    const float chip_h = 19 * s_;
    auto chip = [&](const std::string& text, uint32_t color, bool outline) {
      if (nx + ChipWidth(fonts_, text, chip_h) > limit) return;
      nx += Chip(c, fonts_, text, nx, name_y - 15 * s_, chip_h, color, s_, outline) + 5 * s_;
    };
    chip(L_.PartyShort(p), p.color, false);
    if (leader) {
      chip(L_.T(final ? "race.elected" : "race.leading"), final ? 0xFF2E9E5B : 0xFFE08E0B, false);
    }
    if (cand.incumbent) chip(L_.T("race.incumbent"), kAccent, true);
    std::string sub = L_.CandidateAltName(cand);
    if (!cand.endorsed_by.empty()) {
      std::string backers;
      for (const auto& e : cand.endorsed_by) {
        backers += (backers.empty() ? "" : L_.ListSeparator()) + L_.PartyShort(m.data->party(e));
      }
      sub = Fmt(L_.T("race.backed"), {backers}) + " · " + sub;
    }
    DrawText(c, Ellipsize(fonts_->Regular(12 * s_), sub, w - photo - 120 * s_), tx,
             name_y + 17 * s_, fonts_->Regular(12 * s_), kText3);

    const float bar_y = card.fBottom - 16 * s_;
    const float share = static_cast<float>(t.Share(i));
    Bar(c, SkRect::MakeXYWH(tx, bar_y, w - photo - 16 * s_, 7 * s_), share, p.color);
    // Votes, right aligned.
    DrawText(c, votes_text, x + w, name_y, fonts_->Bold(19 * s_), kText, Align::kRight);
    DrawText(c, counting ? Percent(share) : "—", x + w, name_y + 18 * s_, fonts_->Regular(13 * s_),
             kText2, Align::kRight);
    y += card_h;
  }
  // Footnote.
  y = panel.fBottom - pad - 4 * s_;
  const std::string note =
      counting ? Fmt(L_.T("race.totals"), {FormatThousands(t.TotalVotes()), FormatThousands(t.eligible)})
               : std::string(L_.T("race.order_note"));
  DrawText(c, Ellipsize(fonts_->Regular(12 * s_), note, w), x, y, fonts_->Regular(12 * s_), kText3);
}

void Dashboard::DrawLabels(SkCanvas* c, const DashboardModel& m) {
  std::vector<MapLabel> labels = m.labels;
  std::sort(labels.begin(), labels.end(),
            [](const MapLabel& a, const MapLabel& b) { return a.priority > b.priority; });
  std::vector<SkRect> placed;
  const float panel_x = Layout(m.width, m.height).panel_x;
  for (const MapLabel& l : labels) {
    const geo::Region& r = m.tree->region(l.region);
    const bool county = r.level == geo::Level::kCounty;
    const bool hovered = l.region == m.hover;
    const SkFont f = county || hovered ? fonts_->Bold((county ? 15 : 13.5f) * s_)
                                       : fonts_->Regular(13 * s_);
    const std::string name = L_.RegionName(r);
    const float tw = TextWidth(f, name);
    const SkRect rect = SkRect::MakeXYWH(l.x - tw / 2 - 3 * s_, l.y - 14 * s_, tw + 6 * s_,
                                         18 * s_);
    if (rect.fRight > panel_x || rect.fLeft < 0 || rect.fTop < 0 || rect.fBottom > m.height) {
      continue;
    }
    bool overlaps = false;
    for (const SkRect& p : placed) {
      if (SkRect::Intersects(p, rect)) {
        overlaps = true;
        break;
      }
    }
    if (overlaps && !hovered) continue;
    placed.push_back(rect);
    SkPaint halo;
    halo.setAntiAlias(true);
    halo.setStyle(SkPaint::kStroke_Style);
    halo.setStrokeWidth(3.5f * s_);
    halo.setStrokeJoin(SkPaint::kRound_Join);
    halo.setColor(SkColorSetARGB(200, 5, 12, 22));
    const float x0 = l.x - tw / 2;
    c->drawSimpleText(name.data(), name.size(), SkTextEncoding::kUTF8, x0, l.y, f, halo);
    DrawText(c, name, x0, l.y, f, hovered ? kAccent : kText);
  }
}

void Dashboard::DrawInsets(SkCanvas* c, const DashboardModel& m) {
  for (const InsetFrame& f : m.insets) {
    SkPathBuilder b;
    b.moveTo(f.x[0], f.y[0]);
    for (int k = 1; k < 4; ++k) b.lineTo(f.x[k], f.y[k]);
    b.close();
    SkPaint p = Fill(SkColorSetARGB(150, 160, 176, 195));
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1.2f * s_);
    const float intervals[2] = {6 * s_, 5 * s_};
    p.setPathEffect(SkDashPathEffect::Make(intervals, 0));
    c->drawPath(b.detach(), p);
    DrawText(c, L_.T("inset"), f.x[3] + 4 * s_, f.y[3] - 5 * s_, fonts_->Regular(10.5f * s_),
             SkColorSetARGB(170, 160, 176, 195));
  }
}

void Dashboard::DrawTooltip(SkCanvas* c, const DashboardModel& m) {
  if (m.hover < 0) return;
  const geo::Region& r = m.tree->region(m.hover);
  const Race* race = RaceFor(m, m.hover);
  const float pad = 10 * s_;
  const float w = 250 * s_;
  std::vector<std::pair<std::string, SkColor>> lines;
  const char* level = L_.T(r.level == geo::Level::kCounty ? "level.county"
                           : r.level == geo::Level::kTown ? "level.town"
                                                          : "level.village");
  std::string title = L_.RegionName(r);
  if (race && r.level != geo::Level::kCounty) {
    const std::string county =
        L_.RegionName(m.tree->region(m.tree->AncestorAt(m.hover, geo::Level::kCounty)));
    title = m.lang == Lang::kEn ? title + ", " + county : county + " " + title;
  }
  float h = pad * 2 + 40 * s_;
  const Tally* t = race ? &m.results->RaceTally(*race, m.hover) : nullptr;
  const bool has_votes = t && HasVotes(*t);
  if (has_votes) h += 22 * s_ * std::min<size_t>(3, race->candidates.size()) + 8 * s_;
  else h += 20 * s_;
  float x = m.mouse_x + 18 * s_;
  float y = m.mouse_y + 18 * s_;
  if (x + w > Layout(m.width, m.height).panel_x - 8 * s_) x = m.mouse_x - w - 18 * s_;
  if (y + h > m.height - 30 * s_) y = m.mouse_y - h - 12 * s_;
  const SkRect box = SkRect::MakeXYWH(x, y, w, h);
  Panel(c, box, 10 * s_);
  float ty = y + pad + 16 * s_;
  const float level_w = TextWidth(fonts_->Regular(11.5f * s_), level) + 8 * s_;
  DrawText(c, Ellipsize(fonts_->Bold(16 * s_), title, w - 2 * pad - level_w), x + pad, ty,
           fonts_->Bold(16 * s_), kText);
  DrawText(c, level, x + w - pad, ty, fonts_->Regular(11.5f * s_), kText3, Align::kRight);
  ty += 18 * s_;
  DrawText(c, Ellipsize(fonts_->Regular(11.5f * s_), m.lang == Lang::kEn ? r.name_zh : r.name_en,
                     w - 2 * pad),
           x + pad, ty, fonts_->Regular(11.5f * s_), kText3);
  if (has_votes) {
    ty += 8 * s_;
    const std::vector<int> rank = t->Ranking();
    for (size_t k = 0; k < std::min<size_t>(3, rank.size()); ++k) {
      const Candidate& cand = race->candidates[rank[k]];
      const Party& p = m.data->party(cand.party);
      ty += 22 * s_;
      c->drawCircle(x + pad + 5 * s_, ty - 5 * s_, 5 * s_, Fill(p.color));
      DrawText(c, Ellipsize(fonts_->Regular(13.5f * s_), L_.CandidateName(cand), w - 90 * s_),
               x + pad + 16 * s_, ty, fonts_->Regular(13.5f * s_), kText);
      DrawText(c, Percent(t->Share(rank[k])), x + w - pad, ty, fonts_->Bold(13.5f * s_), kText,
               Align::kRight);
    }
  } else {
    ty += 22 * s_;
    const bool can_enter = !r.children.empty();
    DrawText(c, L_.T(race ? (can_enter ? "tip.no_votes_enter" : "tip.no_votes") : "tip.enter"),
             x + pad, ty, fonts_->Regular(12.5f * s_), kText2);
  }
}

void Dashboard::DrawFooter(SkCanvas* c, const DashboardModel& m) {
  const float y = m.height - 12 * s_;
  const SkFont f = fonts_->Regular(12 * s_);
  const float lang_w = DrawText(c, std::string("◐ ") + LangNativeName(m.lang), 16 * s_, y,
                              fonts_->Bold(12 * s_), kAccent) + 12 * s_;
  DrawText(c,
           Ellipsize(f, Fmt(L_.T("footer.controls"), {ColorModeName(m.lang, m.mode)}),
                     m.width * 0.62f - lang_w),
           16 * s_ + lang_w, y, f, kText2);
  char buf[96];
  std::snprintf(buf, sizeof buf, "%.0f fps · ", m.fps);
  std::string right = buf + m.gpu + " · " + m.source;
  if (!m.source_error.empty()) right = "⚠ " + m.source_error + " · " + right;
  DrawText(c, Ellipsize(f, right, m.width * 0.34f), m.width - 16 * s_, y, f,
           m.source_error.empty() ? kText3 : SkColorSetRGB(255, 120, 100), Align::kRight);

  // Legend (bottom-left, above the footer).
  float lx = 16 * s_;
  const float ly = m.height - 40 * s_;
  if (m.mode == ColorMode::kLeader) {
    std::vector<std::string> shown;
    for (const Race& r : m.data->races()) {
      for (const Candidate& cand : r.candidates) {
        if (std::find(shown.begin(), shown.end(), cand.party) == shown.end()) {
          shown.push_back(cand.party);
        }
      }
    }
    for (const Party& p : m.data->parties()) {
      if (std::find(shown.begin(), shown.end(), p.code) == shown.end()) continue;
      c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(lx, ly - 10 * s_, 12 * s_, 12 * s_),
                                       3 * s_, 3 * s_),
                   Fill(p.color));
      lx += 16 * s_;
      lx += DrawText(c, L_.PartyShort(p), lx, ly, fonts_->Regular(12 * s_), kText2) + 12 * s_;
    }
  } else {
    const uint32_t hi = m.mode == ColorMode::kProgress   ? 0xFFF5A623
                        : m.mode == ColorMode::kTurnout ? 0xFF2EC4B6
                                                        : kAgree;
    const uint32_t lo = m.mode == ColorMode::kReferendum ? kDisagree : kNeutral;
    const SkColor4f cols[2] = {SkColor4f::FromColor(lo), SkColor4f::FromColor(hi)};
    const SkPoint pts[2] = {{lx, 0}, {lx + 160 * s_, 0}};
    SkPaint p;
    p.setAntiAlias(true);
    p.setShader(SkShaders::LinearGradient(
        pts, SkGradient(SkGradient::Colors(cols, {}, SkTileMode::kClamp), {})));
    c->drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(lx, ly - 10 * s_, 160 * s_, 12 * s_), 4 * s_,
                                     4 * s_),
                 p);
    const char* lo_txt = m.mode == ColorMode::kReferendum ? L_.T("ref.disagree")
                         : m.mode == ColorMode::kTurnout  ? "40%"
                                                          : "0%";
    const char* hi_txt = m.mode == ColorMode::kReferendum ? L_.T("ref.agree")
                         : m.mode == ColorMode::kTurnout  ? "80%"
                                                          : "100%";
    DrawText(c, lo_txt, lx, ly - 14 * s_, fonts_->Regular(11 * s_), kText3);
    DrawText(c, hi_txt, lx + 160 * s_, ly - 14 * s_, fonts_->Regular(11 * s_), kText3,
             Align::kRight);
    DrawText(c, ColorModeName(m.lang, m.mode), lx + 170 * s_, ly, fonts_->Regular(12 * s_), kText2);
  }
}

void Dashboard::DrawHelp(SkCanvas* c, const DashboardModel& m) {
  const float w = 520 * s_, h = 360 * s_;
  const float x = (Layout(m.width, m.height).panel_x - w) / 2, y = (m.height - h) / 2;
  Panel(c, SkRect::MakeXYWH(x, y, w, h), 14 * s_);
  const char* keys[] = {"help.title", "help.1", "help.2", "help.3", "help.4",
                        "help.5",     "help.6", "help.7", "help.8", "help.9"};
  float ty = y + 42 * s_;
  for (size_t i = 0; i < std::size(keys); ++i) {
    const SkFont f = i == 0 ? fonts_->Bold(20 * s_) : fonts_->Regular(15 * s_);
    DrawText(c, Ellipsize(f, L_.T(keys[i]), w - 48 * s_), x + 24 * s_, ty, f,
             i == 0 ? kAccent : kText);
    ty += i == 0 ? 38 * s_ : 30 * s_;
  }
}

}  // namespace twn::ui
