#include "src/election/results_source.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
                                               double duration_s)
    : data_(data), tree_(tree), seed_(seed), duration_s_(std::max(1.0, duration_s)) {}

void SimulatedResultsSource::SeekProgress(double p) {
  progress_ = std::clamp(p, 0.0, 1.0);
  dirty_ = true;
}

std::shared_ptr<const ResultsSnapshot> SimulatedResultsSource::Poll(double now) {
  if (last_now_ >= 0 && !paused_ && progress_ < 1.0) {
    progress_ = std::min(1.0, progress_ + (now - last_now_) / duration_s_);
    dirty_ = true;
  }
  last_now_ = now;
  // Emit at most ~4 snapshots per second, like a real feed.
  if (!dirty_ || now - last_emit_ < 0.25) return nullptr;
  last_emit_ = now;
  dirty_ = false;
  auto snap = SnapshotAt(progress_);
  snap->version = ++version_;
  return snap;
}

std::string SimulatedResultsSource::Describe() const {
  char buf[64];
  std::snprintf(buf, sizeof buf, "SIMULATION %.0f%%%s", progress_ * 100, paused_ ? " (paused)" : "");
  return buf;
}

std::shared_ptr<ResultsSnapshot> SimulatedResultsSource::SnapshotAt(double p) const {
  auto snap = std::make_shared<ResultsSnapshot>();
  snap->simulated = true;
  snap->source = "SIMULATION - synthetic numbers, not real results";
  snap->status = p <= 0 ? ResultsStatus::kPreElection
                 : p >= 1 ? ResultsStatus::kFinal
                          : ResultsStatus::kCounting;
  char buf[32];
  const int minutes = static_cast<int>(p * 240);  // 16:00 -> ~20:00
  std::snprintf(buf, sizeof buf, "2026-11-28T%02d:%02d:00+08:00", 16 + minutes / 60, minutes % 60);
  snap->updated_at = buf;

  for (const Race& race : data_->races()) {
    const int county = tree_->FindByCode(race.county_code);
    if (county < 0) continue;
    // Party-blind random candidate strengths, skewed so races have a spread.
    std::vector<double> strength(race.candidates.size());
    for (size_t i = 0; i < race.candidates.size(); ++i) {
      const double u = Unit(HashString(race.candidates[i].id, seed_));
      strength[i] = 0.05 + std::pow(u, 2.2);
    }
    auto& regions = snap->races[race.id];
    for (int town : tree_->region(county).children) {
      const auto& villages = tree_->region(town).children;
      std::vector<int> units = villages.empty() ? std::vector<int>{town} : villages;
      for (int v : units) {
        const geo::Region& vr = tree_->region(v);
        const uint64_t h = HashString(vr.code, seed_);
        Tally t;
        t.votes.assign(race.candidates.size(), 0);
        t.eligible = 600 + static_cast<int64_t>(Unit(Mix(h)) * 5400);
        t.units_total = 1;
        t.has_data = true;
        const double report_at = 0.02 + 0.96 * Unit(Mix(h + 1));
        if (p >= report_at) {
          t.units_counted = 1;
          const double turnout = 0.52 + 0.22 * Unit(Mix(h + 2));
          t.ballots_cast = static_cast<int64_t>(t.eligible * turnout);
          const int64_t valid = static_cast<int64_t>(t.ballots_cast * 0.985);
          std::vector<double> w(race.candidates.size());
          double sum = 0;
          for (size_t i = 0; i < w.size(); ++i) {
            const double local = 0.6 + 0.8 * Unit(Mix(h + 17 * (i + 3)));
            w[i] = strength[i] * local;
            sum += w[i];
          }
          for (size_t i = 0; i < w.size(); ++i) {
            t.votes[i] = static_cast<int64_t>(std::llround(valid * w[i] / sum));
          }
        }
        regions[vr.code] = std::move(t);
      }
    }
  }

  for (const Referendum& ref : data_->info().referendums) {
    auto& regions = snap->referendums[ref.id];
    const double national_lean = Unit(HashString(ref.id, seed_));
    for (int county : tree_->nation().children) {
      for (int town : tree_->region(county).children) {
        const geo::Region& tr = tree_->region(town);
        const uint64_t h = HashString(tr.code + ref.id, seed_);
        RefTally t;
        t.units_total = 1;
        t.has_data = true;
        t.eligible = 8000 + static_cast<int64_t>(Unit(Mix(h)) * 120000);
        if (p >= 0.03 + 0.95 * Unit(Mix(h + 5))) {
          t.units_counted = 1;
          t.ballots_cast = static_cast<int64_t>(t.eligible * (0.40 + 0.2 * Unit(Mix(h + 6))));
          const double agree = 0.35 + 0.3 * national_lean + 0.1 * (Unit(Mix(h + 7)) - 0.5);
          t.agree = static_cast<int64_t>(t.ballots_cast * 0.98 * agree);
          t.disagree = static_cast<int64_t>(t.ballots_cast * 0.98) - t.agree;
        }
        regions[tr.code] = t;
      }
    }
  }
  return snap;
}

}  // namespace twn::election
