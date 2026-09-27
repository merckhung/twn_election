// Election-night simulation and event detection.
#include <map>
#include <set>

#include "gtest/gtest.h"
#include "src/election/events.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/geo/region_tree.h"

namespace twn::election {
namespace {

class NightTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    data_ = new ElectionData();
    std::string error;
    ASSERT_TRUE(data_->Load("data/election/2026", &error)) << error;
    std::string json;
    ASSERT_TRUE(ReadFile("data/map/villages-10t.json", &json));
    tree_ = new geo::RegionTree();
    ASSERT_TRUE(tree_->LoadTopoJson(json, &error)) << error;
  }
  static void TearDownTestSuite() {
    delete data_;
    delete tree_;
  }
  static ElectionData* data_;
  static geo::RegionTree* tree_;
};

ElectionData* NightTest::data_ = nullptr;
geo::RegionTree* NightTest::tree_ = nullptr;

TEST_F(NightTest, SpeedSnapsToPowersOfTwo) {
  SimulatedResultsSource sim(data_, tree_, 1, 64);
  EXPECT_EQ(sim.speed(), 64);
  sim.set_speed(100);
  EXPECT_EQ(sim.speed(), 128);
  sim.set_speed(0.3);
  EXPECT_EQ(sim.speed(), 1);
  sim.set_speed(1e9);
  EXPECT_EQ(sim.speed(), 4096);
  sim.SlowDown();
  EXPECT_EQ(sim.speed(), 2048);
  sim.SpeedUp();
  EXPECT_EQ(sim.speed(), 4096);
}

TEST_F(NightTest, ClockAdvancesWithSpeed) {
  SimulatedResultsSource sim(data_, tree_, 1, 60);  // -> x64
  sim.Poll(0);
  sim.Poll(60);  // one real minute at x64 = 64 simulated minutes
  EXPECT_NEAR(sim.clock_minutes(), 64, 1e-6);
  sim.set_paused(true);
  sim.Poll(120);
  EXPECT_NEAR(sim.clock_minutes(), 64, 1e-6);
  EXPECT_EQ(SimulatedResultsSource::ClockLabel(64), "17:04");
}

TEST_F(NightTest, CountRunsFromCloseOfPollsToAboutElevenPm) {
  SimulatedResultsSource sim(data_, tree_, 20261128, 64);
  EXPECT_GT(sim.station_count(), 10000);
  EXPECT_LE(sim.last_report_minute(), SimulatedResultsSource::kCountMinutes);
  EXPECT_GT(sim.last_report_minute(), 360);  // tail past 22:00
  EXPECT_EQ(sim.SnapshotAtClock(5)->status, ResultsStatus::kPreElection);
  EXPECT_EQ(sim.SnapshotAtClock(120)->status, ResultsStatus::kCounting);
  EXPECT_EQ(sim.SnapshotAtClock(420)->status, ResultsStatus::kFinal);
  EXPECT_EQ(sim.SnapshotAtClock(120)->updated_at, "2026-11-28T18:00:00+08:00");
}

TEST_F(NightTest, WholeNightProducesTheExpectedStory) {
  SimulatedResultsSource sim(data_, tree_, 20261128, 64);
  EventTracker tracker(tree_);
  std::map<EventType, int> counts;
  std::map<std::string, std::string> victory_time, final_time;
  for (int minute = 0; minute <= 420; minute += 2) {
    auto snap = sim.SnapshotAtClock(minute);
    ResultsView view(data_, tree_, snap);
    const EventBatch batch = tracker.Update(view, data_->races());
    EXPECT_EQ(batch.baseline, minute == 0);
    for (const ElectionEvent& e : batch.events) {
      counts[e.type]++;
      if (e.type == EventType::kVictoryDeclared) victory_time[e.race->id] = e.time_label;
      if (e.type == EventType::kFinal) final_time[e.race->id] = e.time_label;
    }
  }
  const int races = static_cast<int>(data_->races().size());
  EXPECT_EQ(counts[EventType::kFirstReturns], races);
  EXPECT_EQ(counts[EventType::kFinal], races);
  EXPECT_EQ(counts[EventType::kCalled], races);
  EXPECT_EQ(counts[EventType::kVictoryDeclared], races);
  EXPECT_GE(counts[EventType::kLeadChange], 5);
  EXPECT_GE(counts[EventType::kConcession], races / 2);
  // Campaigns usually claim victory before the count completes (and always
  // before the CEC's official announcement, days later); photo finishes may
  // only be claimed once every station is in.
  int early = 0;
  for (const auto& [race, t] : victory_time) {
    ASSERT_TRUE(final_time.count(race));
    early += t < final_time[race];
  }
  EXPECT_GE(early, races - 3);
}

TEST_F(NightTest, DeclarationsRoundTripThroughJson) {
  SimulatedResultsSource sim(data_, tree_, 20261128, 64);
  auto snap = sim.SnapshotAtClock(400);
  ASSERT_FALSE(snap->declarations.empty());
  const std::string json = ResultsToJson(*snap, *data_);
  ResultsSnapshot parsed;
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, &parsed, &error)) << error;
  ASSERT_EQ(parsed.declarations.size(), snap->declarations.size());
  EXPECT_EQ(parsed.declarations[0].candidate_id, snap->declarations[0].candidate_id);
  EXPECT_EQ(parsed.declarations[0].type, snap->declarations[0].type);
}

TEST_F(NightTest, InflowHistoryMatchesSnapshots) {
  SimulatedResultsSource sim(data_, tree_, 5, 64);
  const auto buckets = sim.InflowHistory(5, 180);
  int64_t sum = 0;
  for (int64_t b : buckets) sum += b;
  auto snap = sim.SnapshotAtClock(180);
  ResultsView view(data_, tree_, snap);
  int64_t total = 0;
  for (const Race& r : data_->races()) total += view.RaceTally(r, tree_->FindByCode(r.county_code)).TotalVotes();
  EXPECT_EQ(sum, total);
}

// Hand-made snapshots for the tracker rules.
class TrackerTest : public NightTest {
 protected:
  std::shared_ptr<ResultsSnapshot> Snap(int64_t a, int64_t b, int counted, int total,
                                        const std::string& time) {
    auto s = std::make_shared<ResultsSnapshot>();
    const Race& race = *data_->RaceForCounty("63000");
    Tally t;
    t.votes.assign(race.candidates.size(), 0);
    t.votes[1] = a;  // 沈伯洋
    t.votes[4] = b;  // 蔣萬安 (incumbent)
    t.units_counted = counted;
    t.units_total = total;
    t.has_data = true;
    s->races[race.id]["63000"] = t;
    s->updated_at = "2026-11-28T" + time + ":00+08:00";
    return s;
  }
  EventBatch Feed(EventTracker& tracker, std::shared_ptr<ResultsSnapshot> s) {
    ResultsView view(data_, tree_, s);
    return tracker.Update(view, data_->races());
  }
};

TEST_F(TrackerTest, LeadChangeNeedsAMargin) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "16:00"));  // baseline
  auto b = Feed(tracker, Snap(1000, 900, 10, 100, "16:30"));
  ASSERT_EQ(b.events.size(), 1u);
  EXPECT_EQ(b.events[0].type, EventType::kFirstReturns);
  EXPECT_EQ(b.events[0].time_label, "16:30");
  // Overtaken by 10 votes: below the hysteresis margin, no event.
  b = Feed(tracker, Snap(2000, 2010, 20, 100, "16:40"));
  for (const auto& e : b.events) EXPECT_NE(e.type, EventType::kLeadChange);
  // Clear overtake: lead change (plus incumbent no longer trailing is silent).
  b = Feed(tracker, Snap(3000, 3500, 30, 100, "16:50"));
  bool lead_change = false;
  for (const auto& e : b.events) {
    if (e.type == EventType::kLeadChange) {
      lead_change = true;
      EXPECT_EQ(e.leader, 4);
      EXPECT_EQ(e.previous, 1);
      EXPECT_TRUE(IsBreaking(e.type));
    }
  }
  EXPECT_TRUE(lead_change);
}

TEST_F(TrackerTest, IncumbentTrailingCalledAndFinal) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "16:00"));
  Feed(tracker, Snap(100, 50, 1, 100, "16:20"));
  auto b = Feed(tracker, Snap(40000, 30000, 40, 100, "18:00"));
  std::set<EventType> types;
  for (const auto& e : b.events) types.insert(e.type);
  EXPECT_TRUE(types.count(EventType::kIncumbentTrailing));
  EXPECT_FALSE(types.count(EventType::kCalled));  // 60% still out
  b = Feed(tracker, Snap(90000, 30000, 90, 100, "21:00"));
  types.clear();
  for (const auto& e : b.events) types.insert(e.type);
  EXPECT_TRUE(types.count(EventType::kCalled));
  EXPECT_TRUE(tracker.IsCalled("63000-mayor"));
  b = Feed(tracker, Snap(99000, 33000, 100, 100, "22:00"));
  ASSERT_EQ(b.events.size(), 1u);
  EXPECT_EQ(b.events[0].type, EventType::kFinal);
}

TEST_F(TrackerTest, DeclarationsBecomeEvents) {
  EventTracker tracker(tree_);
  Feed(tracker, Snap(0, 0, 0, 100, "16:00"));
  auto s = Snap(5000, 3000, 50, 100, "19:00");
  s->declarations.push_back({"63000-mayor", "63000-02", Declaration::Type::kVictory,
                             "2026-11-28T19:05:00+08:00"});
  auto b = Feed(tracker, s);
  bool victory = false;
  for (const auto& e : b.events) {
    if (e.type == EventType::kVictoryDeclared) {
      victory = true;
      EXPECT_EQ(e.leader, 1);
      EXPECT_EQ(e.time_label, "19:05");
    }
  }
  EXPECT_TRUE(victory);
  // Same declaration again: no duplicate.
  auto s2 = Snap(6000, 3500, 60, 100, "19:10");
  s2->declarations = s->declarations;
  b = Feed(tracker, s2);
  for (const auto& e : b.events) EXPECT_NE(e.type, EventType::kVictoryDeclared);
}

TEST(Events, TimeLabels) {
  EXPECT_EQ(TimeLabel("2026-11-28T18:42:00+08:00"), "18:42");
  EXPECT_EQ(TimeLabel(""), "");
  EXPECT_DOUBLE_EQ(MinutesAfterClose("2026-11-28T18:42:00+08:00"), 162);
  EXPECT_DOUBLE_EQ(MinutesAfterClose("garbage"), -1);
}

}  // namespace
}  // namespace twn::election
