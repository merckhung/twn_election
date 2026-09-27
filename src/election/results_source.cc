#include "src/election/results_source.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace twn::election {
namespace {

// SplitMix64: tiny, deterministic hash for reproducible simulations.
uint64_t Mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

uint64_t HashString(std::string_view s, uint64_t seed) {
  uint64_t h = seed ^ 0xCBF29CE484222325ull;
  for (unsigned char c : s) h = (h ^ c) * 0x100000001B3ull;
  return Mix(h);
}

double Unit(uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

}  // namespace

FileResultsSource::FileResultsSource(const ElectionData* data, std::string path,
                                     double poll_interval_s)
    : data_(data), path_(std::move(path)), poll_interval_s_(poll_interval_s) {}

std::shared_ptr<const ResultsSnapshot> FileResultsSource::Poll(double now) {
  if (now - last_poll_ < poll_interval_s_) return nullptr;
  last_poll_ = now;
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(path_, ec);
  if (ec) {
    last_error_ = "cannot stat " + path_;
    return nullptr;
  }
  if (loaded_once_ && mtime == last_mtime_) return nullptr;
  std::string text;
  if (!ReadFile(path_, &text)) {
    last_error_ = "cannot read " + path_;
    return nullptr;
  }
  auto snap = std::make_shared<ResultsSnapshot>();
  std::string error;
  if (!ParseResultsJson(text, *data_, snap.get(), &error)) {
    // Keep the previous snapshot; a writer may be mid-way through the file.
    last_error_ = error;
    return nullptr;
  }
  last_error_.clear();
  last_mtime_ = mtime;
  loaded_once_ = true;
  snap->version = ++version_;
  return snap;
}

std::string FileResultsSource::Describe() const { return "file: " + path_; }

SimulatedResultsSource::SimulatedResultsSource(const ElectionData* data,
                                               const geo::RegionTree* tree, uint64_t seed,
                                               double speed)
    : data_(data), tree_(tree), seed_(seed) {
  set_speed(speed);
  Build();
}

void SimulatedResultsSource::Build() {
  village_units_.assign(tree_->size(), 0);
  const auto& races = data_->races();
  const double national_lean = Unit(Mix(seed_ ^ 0x5EED));
  for (size_t ri = 0; ri < races.size(); ++ri) {
    const Race& race = races[ri];
    const int county = tree_->FindByCode(race.county_code);
    if (county < 0) continue;
    const size_t n = race.candidates.size();
    const uint64_t rh = HashString(race.id, seed_);

    // Party-blind random strengths; the two strongest are made close so the
    // count is competitive.
    std::vector<double> strength(n);
    for (size_t i = 0; i < n; ++i) {
      strength[i] = 0.04 + std::pow(Unit(HashString(race.candidates[i].id, seed_)), 2.4);
    }
    std::vector<int> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return strength[a] > strength[b]; });
    const int front_a = order[0];
    const int front_b = n > 1 ? order[1] : order[0];
    // About a third of races are neck and neck; the rest are competitive.
    const double tight = Unit(Mix(rh + 9));
    strength[front_b] = strength[front_a] * (tight < 0.3 ? 0.985 + 0.02 * Unit(Mix(rh + 1))
                                                         : 0.86 + 0.14 * Unit(Mix(rh + 1)));
    // Correlation between reporting time and lean: > 0 means early stations
    // favour B and late ones favour A (so A comes from behind).
    const double u = Unit(Mix(rh + 2));
    const double corr = u < 0.35 ? 0.0 : (u < 0.7 ? 1.0 : -1.0) * (0.35 + 0.5 * Unit(Mix(rh + 3)));

    for (int town : tree_->region(county).children) {
      const auto& villages = tree_->region(town).children;
      const std::vector<int> units = villages.empty() ? std::vector<int>{town} : villages;
      for (int v : units) {
        const uint64_t vh = HashString(tree_->region(v).code, seed_);
        // Village-level timing: when this neighbourhood's stations report.
        // Kumaraswamy(1.6, 3.2): trickle from 16:15, peak ~18:00, tail to ~23:00.
        const double k = std::pow(1.0 - std::pow(1.0 - Unit(Mix(vh + 1)), 1.0 / 3.2), 1.0 / 1.6);
        const double lean = corr * (1.0 - 2.0 * k) + 0.35 * (Unit(Mix(vh + 2)) * 2 - 1);
        const int count = 1 + static_cast<int>(Unit(Mix(vh + 3)) * 3.2);
        village_units_[v] = count;
        for (int s = 0; s < count; ++s) {
          const uint64_t sh = Mix(vh + 101 * (s + 1));
          Station st;
          const double jitter = (Unit(Mix(sh + 4)) - 0.5) * 30.0;
          st.minute = static_cast<float>(std::clamp(14.0 + 395.0 * k + jitter, 12.0, 412.0));
          st.race = static_cast<int>(ri);
          st.village = v;
          st.eligible = 500 + static_cast<int32_t>(Unit(Mix(sh + 5)) * 1900);
          st.cast = static_cast<int32_t>(st.eligible * (0.55 + 0.2 * Unit(Mix(sh + 6))));
          const int32_t valid = static_cast<int32_t>(st.cast * 0.985);
          std::vector<double> w(n);
          double sum = 0;
          for (size_t i = 0; i < n; ++i) {
            w[i] = strength[i] * (0.7 + 0.6 * Unit(Mix(sh + 17 * (i + 7))));
            if (static_cast<int>(i) == front_a) w[i] *= 1.0 - 0.3 * lean;
            if (static_cast<int>(i) == front_b && front_b != front_a) w[i] *= 1.0 + 0.3 * lean;
            sum += w[i];
          }
          st.votes_at = static_cast<uint32_t>(votes_.size());
          for (size_t i = 0; i < n; ++i) {
            votes_.push_back(static_cast<int32_t>(std::llround(valid * w[i] / sum)));
          }
          // Referendum ballot at the same station (all voters get one).
          st.ref_eligible = st.eligible;
          const int32_t ref_valid = static_cast<int32_t>(st.cast * 0.93 * 0.97);
          const double agree = std::clamp(
              0.38 + 0.26 * national_lean + 0.12 * (Unit(Mix(sh + 8)) - 0.5) + 0.04 * lean, 0.05,
              0.95);
          st.agree = static_cast<int32_t>(ref_valid * agree);
          st.disagree = ref_valid - st.agree;
          stations_.push_back(st);
        }
      }
    }
  }
  std::stable_sort(stations_.begin(), stations_.end(),
                   [](const Station& a, const Station& b) { return a.minute < b.minute; });
  last_report_ = stations_.empty() ? 0 : stations_.back().minute;
  PlanDeclarations();
}

// Walks the count in time order and decides when campaigns speak: the leader
// declares victory once their margin comfortably exceeds the votes still out
// (a looser rule than a media projection, so it can come early, and
// occasionally prematurely); the runner-up concedes some minutes later if
// the lead holds.
void SimulatedResultsSource::PlanDeclarations() {
  const auto& races = data_->races();
  std::vector<std::vector<int64_t>> votes(races.size());
  std::vector<int> counted(races.size(), 0), total_units(races.size(), 0);
  std::vector<int64_t> total_votes(races.size(), 0);
  std::vector<int> declared(races.size(), -1);
  std::vector<float> declared_at(races.size(), 0);
  std::vector<bool> conceded(races.size(), false);
  for (size_t r = 0; r < races.size(); ++r) votes[r].assign(races[r].candidates.size(), 0);
  for (const Station& st : stations_) total_units[st.race]++;

  auto leaders = [&](int r, int* first, int* second) {
    *first = *second = -1;
    for (int i = 0; i < static_cast<int>(votes[r].size()); ++i) {
      if (*first < 0 || votes[r][i] > votes[r][*first]) {
        *second = *first;
        *first = i;
      } else if (*second < 0 || votes[r][i] > votes[r][*second]) {
        *second = i;
      }
    }
  };
  for (const Station& st : stations_) {
    const int r = st.race;
    for (size_t i = 0; i < votes[r].size(); ++i) {
      votes[r][i] += votes_[st.votes_at + i];
      total_votes[r] += votes_[st.votes_at + i];
    }
    counted[r]++;
    int a, b;
    leaders(r, &a, &b);
    if (b < 0) continue;
    const int64_t margin = votes[r][a] - votes[r][b];
    const double per_unit = static_cast<double>(total_votes[r]) / counted[r];
    const double remaining = per_unit * (total_units[r] - counted[r]);
    const double progress = static_cast<double>(counted[r]) / total_units[r];
    const uint64_t h = HashString(races[r].id, seed_ ^ 0xDEC1);
    // Campaigns speak once their own tallies look safe: a clear lead with
    // around half of the stations in, well before media projections.
    if (declared[r] < 0 && progress >= 0.4 &&
        (margin > 0.3 * remaining || (progress >= 0.55 && margin > 0.04 * total_votes[r]))) {
      declared[r] = a;
      // Speech at campaign HQ 5-25 minutes later.
      declared_at[r] = st.minute + 5 + 20 * static_cast<float>(Unit(Mix(h)));
      declarations_.push_back({declared_at[r], r, a, Declaration::Type::kVictory});
    }
    if (declared[r] == a && !conceded[r] && st.minute > declared_at[r] &&
        margin > 1.05 * remaining) {
      conceded[r] = true;
      declarations_.push_back({st.minute + 3 + 15 * static_cast<float>(Unit(Mix(h + 1))), r, b,
                               Declaration::Type::kConcede});
    }
  }
  std::sort(declarations_.begin(), declarations_.end(),
            [](const PlannedDeclaration& x, const PlannedDeclaration& y) { return x.minute < y.minute; });
}

void SimulatedResultsSource::SeekClock(double minutes) {
  clock_ = std::clamp(minutes, 0.0, kCountMinutes);
  emitted_reported_ = SIZE_MAX;  // force a new snapshot
  emitted_declared_ = SIZE_MAX;
}

void SimulatedResultsSource::set_speed(double s) {
  s = std::clamp(s, kMinSpeed, kMaxSpeed);
  speed_ = std::exp2(std::round(std::log2(s)));
}

std::string SimulatedResultsSource::ClockLabel(double minutes) {
  const int m = static_cast<int>(std::floor(std::clamp(minutes, 0.0, 480.0)));
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d:%02d", 16 + m / 60, m % 60);
  return buf;
}

std::shared_ptr<const ResultsSnapshot> SimulatedResultsSource::Poll(double now) {
  if (last_now_ >= 0 && !paused_ && clock_ < kCountMinutes) {
    clock_ = std::min(kCountMinutes, clock_ + (now - last_now_) * speed_ / 60.0);
  }
  last_now_ = now;
  // Like a real feed: publish at most ~5 times per second, and only when new
  // stations have reported.
  if (now - last_emit_ < 0.2 && emitted_reported_ != SIZE_MAX) return nullptr;
  const size_t reported = static_cast<size_t>(
      std::upper_bound(stations_.begin(), stations_.end(), clock_,
                       [](double t, const Station& s) { return t < s.minute; }) -
      stations_.begin());
  const size_t declared = static_cast<size_t>(
      std::upper_bound(declarations_.begin(), declarations_.end(), clock_,
                       [](double t, const PlannedDeclaration& d) { return t < d.minute; }) -
      declarations_.begin());
  if (reported == emitted_reported_ && declared == emitted_declared_) return nullptr;
  emitted_declared_ = declared;
  last_emit_ = now;
  emitted_reported_ = reported;
  auto snap = SnapshotAtClock(clock_);
  snap->version = ++version_;
  return snap;
}

std::string SimulatedResultsSource::Describe() const {
  char buf[96];
  std::snprintf(buf, sizeof buf, "SIMULATION %s ×%g%s", ClockLabel(clock_).c_str(), speed_,
                paused_ ? " (paused)" : "");
  return buf;
}

std::vector<int64_t> SimulatedResultsSource::InflowHistory(double bucket_minutes,
                                                           double until) const {
  std::vector<int64_t> out;
  const auto& races = data_->races();
  for (const Station& st : stations_) {
    if (st.minute > until) break;
    const size_t b = static_cast<size_t>(st.minute / bucket_minutes);
    if (out.size() <= b) out.resize(b + 1, 0);
    for (size_t i = 0; i < races[st.race].candidates.size(); ++i) out[b] += votes_[st.votes_at + i];
  }
  return out;
}

std::shared_ptr<ResultsSnapshot> SimulatedResultsSource::SnapshotAt(double p) const {
  return SnapshotAtClock(p * kCountMinutes);
}

std::shared_ptr<ResultsSnapshot> SimulatedResultsSource::SnapshotAtClock(double minutes) const {
  minutes = std::clamp(minutes, 0.0, kCountMinutes);
  auto snap = std::make_shared<ResultsSnapshot>();
  snap->simulated = true;
  snap->source = "SIMULATION - synthetic numbers, not real results";
  snap->updated_at = "2026-11-28T" + ClockLabel(minutes) + ":00+08:00";

  const auto& races = data_->races();
  // Every village appears (so progress denominators are right) even before
  // any of its stations report.
  std::vector<Tally*> village_tally(tree_->size(), nullptr);
  std::vector<RefTally*> village_ref(tree_->size(), nullptr);
  const std::string ref_id =
      data_->info().referendums.empty() ? std::string() : data_->info().referendums[0].id;
  for (const Station& st : stations_) {
    if (village_tally[st.village]) continue;
    const Race& race = races[st.race];
    const std::string& code = tree_->region(st.village).code;
    Tally& t = snap->races[race.id][code];
    t.votes.assign(race.candidates.size(), 0);
    t.units_total = village_units_[st.village];
    t.has_data = true;
    village_tally[st.village] = &t;
    if (!ref_id.empty()) {
      RefTally& r = snap->referendums[ref_id][code];
      r.units_total = village_units_[st.village];
      r.has_data = true;
      village_ref[st.village] = &r;
    }
  }
  size_t reported = 0;
  for (const Station& st : stations_) {
    if (st.minute > minutes) break;
    ++reported;
    Tally& t = *village_tally[st.village];
    for (size_t i = 0; i < t.votes.size(); ++i) t.votes[i] += votes_[st.votes_at + i];
    t.eligible += st.eligible;
    t.ballots_cast += st.cast;
    t.units_counted += 1;
    if (RefTally* r = village_ref[st.village]) {
      r->agree += st.agree;
      r->disagree += st.disagree;
      r->eligible += st.ref_eligible;
      r->ballots_cast += st.cast;
      r->units_counted += 1;
    }
  }
  for (const PlannedDeclaration& d : declarations_) {
    if (d.minute > minutes) break;
    const Race& race = races[d.race];
    snap->declarations.push_back({race.id, race.candidates[d.candidate].id, d.type,
                                  "2026-11-28T" + ClockLabel(d.minute) + ":00+08:00"});
  }
  snap->status = reported == 0                ? ResultsStatus::kPreElection
                 : reported == stations_.size() ? ResultsStatus::kFinal
                                                : ResultsStatus::kCounting;
  return snap;
}

}  // namespace twn::election
