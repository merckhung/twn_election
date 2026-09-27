// Turns successive results snapshots into election-night events: first
// returns, lead changes, projected winners and completed counts, plus the
// per-region vote deltas and lead flips the map animates. Works for any
// results source (live file feed or simulation).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/election/results.h"
#include "src/geo/region_tree.h"

namespace twn::election {

// Election-night events. "Breaking" (快訊) events are the ones that change the
// story of a race and get a full-width banner plus a map call-out:
//   kLeadChange        the leader is overtaken (逆轉)
//   kVictoryDeclared   a candidate claims victory from campaign HQ, usually
//                      before the count is complete (自行宣布勝選)
//   kConcession        the runner-up concedes (發表敗選感言)
//   kCalled            the margin exceeds every vote still out (當選確定)
//   kIncumbentTrailing a sitting mayor/magistrate falls behind (現任落後)
// Regular updates get a call-out and a feed line only:
//   kFirstReturns      first votes in a race
//   kCloseRace         margin under 1% with 70%+ counted (差距膠著)
//   kFinal             every unit counted (開票完畢). The CEC's official
//                      announcement of winners follows days later.
enum class EventType {
  kFirstReturns,
  kLeadChange,
  kVictoryDeclared,
  kConcession,
  kCalled,
  kIncumbentTrailing,
  kCloseRace,
  kFinal,
};

bool IsBreaking(EventType t);
const char* EventKey(EventType t);  // i18n key suffix, e.g. "lead_change"

struct ElectionEvent {
  EventType type = EventType::kFirstReturns;
  const Race* race = nullptr;
  int leader = -1;    // candidate index
  int previous = -1;  // previous leader (lead change)
  int64_t margin = 0;
  double progress = 0;     // share of units counted in the race
  std::string time_label;  // "18:42" (from the snapshot's updated_at)
  // Lead change against a candidate who had already declared victory.
  bool after_declaration = false;
};

struct RegionDelta {
  int region = -1;
  int64_t votes_added = 0;
};

struct RegionFlip {
  int region = -1;
  const Race* race = nullptr;
  int leader = -1;
  int previous = -1;
};

struct EventBatch {
  std::vector<ElectionEvent> events;
  std::vector<RegionDelta> deltas;  // counties, towns and villages with new votes
  std::vector<RegionFlip> flips;    // regions whose local leader changed
  int64_t votes_added = 0;          // nationwide, all races
  bool baseline = false;            // first snapshot: state recorded, no events
};

class EventTracker {
 public:
  explicit EventTracker(const geo::RegionTree* tree) : tree_(tree) {}

  // Compares `view` with the previous snapshot seen.
  EventBatch Update(ResultsView& view, const std::vector<Race>& races);

  // True once a race's winner is projected (or the count is complete).
  bool IsCalled(const std::string& race_id) const;
  void Reset();

  // Projection rule: the leader's margin exceeds every vote still to come,
  // estimated from the average votes per counted unit (+15% safety).
  static bool Decided(const Tally& t);

 private:
  struct RegionState {
    int64_t total = 0;
    int leader = -1;
  };
  struct RaceState {
    bool called = false;
    bool final = false;
    bool close_alerted = false;
    bool incumbent_alerted = false;
    int declared = -1;  // candidate who declared victory
    int leader = -1;    // leader as reported in events (with hysteresis)
    size_t declarations_seen = 0;
  };
  const geo::RegionTree* tree_;
  bool has_baseline_ = false;
  std::unordered_map<int, RegionState> regions_;
  std::unordered_map<std::string, RaceState> races_;
};

// "18:42" from "2026-11-28T18:42:00+08:00"; "" when absent.
std::string TimeLabel(const std::string& updated_at);
// Minutes after 16:00 for an updated_at timestamp, or -1.
double MinutesAfterClose(const std::string& updated_at);

}  // namespace twn::election
