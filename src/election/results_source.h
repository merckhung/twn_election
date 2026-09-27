// Where live tallies come from.
//
//  * FileResultsSource: polls a results JSON file (see results.h for the
//    format) and reloads it whenever it changes. Point any feed/scraper at
//    that file, e.g. the converter in tools/.
//  * SimulatedResultsSource: a deterministic, clearly-labelled DEMO that
//    fakes a counting night so the UI can be exercised before 2026-11-28.
//    Candidate strengths are random and party-blind; they are not forecasts.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "src/election/model.h"
#include "src/election/results.h"
#include "src/geo/region_tree.h"

namespace twn::election {

class ResultsSource {
 public:
  virtual ~ResultsSource() = default;
  // Returns a new snapshot when the data changed since the last call.
  virtual std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) = 0;
  virtual std::string Describe() const = 0;
};

class FileResultsSource : public ResultsSource {
 public:
  FileResultsSource(const ElectionData* data, std::string path, double poll_interval_s = 1.0);
  std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) override;
  std::string Describe() const override;
  const std::string& last_error() const { return last_error_; }

 private:
  const ElectionData* data_;
  std::string path_;
  double poll_interval_s_;
  double last_poll_ = -1e9;
  std::filesystem::file_time_type last_mtime_{};
  bool loaded_once_ = false;
  uint64_t version_ = 0;
  std::string last_error_;
};

class SimulatedResultsSource : public ResultsSource {
 public:
  // `duration_s`: wall-clock seconds for the simulated count to finish.
  SimulatedResultsSource(const ElectionData* data, const geo::RegionTree* tree, uint64_t seed,
                         double duration_s);

  std::shared_ptr<const ResultsSnapshot> Poll(double now_seconds) override;
  std::string Describe() const override;

  // Builds the snapshot at counting progress `p` in [0, 1].
  std::shared_ptr<ResultsSnapshot> SnapshotAt(double p) const;

  void set_paused(bool paused) { paused_ = paused; }
  bool paused() const { return paused_; }
  // Jumps the simulated clock to progress `p`.
  void SeekProgress(double p);
  double progress() const { return progress_; }

 private:
  const ElectionData* data_;
  const geo::RegionTree* tree_;
  uint64_t seed_;
  double duration_s_;
  bool paused_ = false;
  double progress_ = 0;
  double last_now_ = -1;
  double last_emit_ = -1e9;
  uint64_t version_ = 0;
  bool dirty_ = true;
};

}  // namespace twn::election
