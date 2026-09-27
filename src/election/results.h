// Vote tallies: snapshot parsing, and aggregation up the region hierarchy.
//
// Results file format (twn_election.results/v1), all counts optional:
// {
//   "schema": "twn_election.results/v1",
//   "status": "pre-election" | "counting" | "final",
//   "source": "free text", "updated_at": "ISO-8601",
//   "races": {
//     "63000-mayor": { "regions": {
//        "<region code>": { "votes": {"<candidate id>": 123, ...},
//                           "eligible": 0, "ballots_cast": 0,
//                           "units_counted": 0, "units_total": 0 } } } },
//   "referendums": {
//     "ref-22": { "regions": { "<region code>": { "agree": 0, "disagree": 0,
//                  "eligible": 0, "ballots_cast": 0,
//                  "units_counted": 0, "units_total": 0 } } } }
// }
// Region codes are the taiwan-atlas codes: "TW", COUNTYCODE (5 digits),
// TOWNCODE (8) or VILLCODE (11). A region without an explicit entry is the
// sum of its children, so feeds may report at any granularity.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "src/election/model.h"
#include "src/geo/region_tree.h"

namespace twn::election {

struct Tally {
  std::vector<int64_t> votes;  // indexed like Race::candidates
  int64_t eligible = 0;
  int64_t ballots_cast = 0;
  int units_counted = 0;  // polling stations (or villages) counted
  int units_total = 0;
  bool has_data = false;

  int64_t TotalVotes() const;
  // Candidate index with most votes, or -1 when no votes.
  int Leader() const;
  // Candidate indices sorted by votes (desc), ties by index.
  std::vector<int> Ranking() const;
  double Share(int candidate) const;
  double Turnout() const;   // ballots_cast / eligible, or 0
  double Progress() const;  // units_counted / units_total, or 0
  void Add(const Tally& other);
};

struct RefTally {
  int64_t agree = 0;
  int64_t disagree = 0;
  int64_t eligible = 0;
  int64_t ballots_cast = 0;
  int units_counted = 0;
  int units_total = 0;
  bool has_data = false;

  void Add(const RefTally& other);
  double Progress() const;
  // Taiwan's referendum threshold: agree > disagree AND agree >= 25% of electorate.
  bool Passes() const;
};

enum class ResultsStatus { kPreElection, kCounting, kFinal };

const char* StatusLabelZh(ResultsStatus s);

struct ResultsSnapshot {
  ResultsStatus status = ResultsStatus::kPreElection;
  std::string source;
  std::string updated_at;
  bool simulated = false;
  uint64_t version = 0;
  // race id -> region code -> tally
  std::unordered_map<std::string, std::unordered_map<std::string, Tally>> races;
  // referendum id -> region code -> tally
  std::unordered_map<std::string, std::unordered_map<std::string, RefTally>> referendums;
};

bool ParseResultsJson(std::string_view json, const ElectionData& data, ResultsSnapshot* out,
                      std::string* error);

std::string ResultsToJson(const ResultsSnapshot& snap, const ElectionData& data);

// Read-only aggregated view over a snapshot (memoised).
class ResultsView {
 public:
  ResultsView(const ElectionData* data, const geo::RegionTree* tree,
              std::shared_ptr<const ResultsSnapshot> snapshot);

  const ResultsSnapshot& snapshot() const { return *snapshot_; }

  // Tally of `race` restricted to `region_id` (which must lie in the race's
  // county, or be the county itself).
  const Tally& RaceTally(const Race& race, int region_id);
  const RefTally& ReferendumTally(const std::string& ref_id, int region_id);

  // The race decided in the county that contains `region_id`, or nullptr.
  const Race* RaceForRegion(int region_id) const;

 private:
  const ElectionData* data_;
  const geo::RegionTree* tree_;
  std::shared_ptr<const ResultsSnapshot> snapshot_;
  std::unordered_map<std::string, std::unordered_map<int, Tally>> race_memo_;
  std::unordered_map<std::string, std::unordered_map<int, RefTally>> ref_memo_;
};

}  // namespace twn::election
