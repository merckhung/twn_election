#include <filesystem>
#include <map>
#include <set>

#include "gtest/gtest.h"
#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/geo/region_tree.h"

namespace twn::election {
namespace {

class ElectionDataTest : public ::testing::Test {
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

ElectionData* ElectionDataTest::data_ = nullptr;
geo::RegionTree* ElectionDataTest::tree_ = nullptr;

TEST_F(ElectionDataTest, TwentyTwoRacesEightyOneCandidates) {
  EXPECT_EQ(data_->info().date, "2026-11-28");
  ASSERT_EQ(data_->races().size(), 22u);
  size_t candidates = 0;
  int municipal = 0, municipal_candidates = 0;
  std::set<std::string> ids;
  for (const Race& r : data_->races()) {
    candidates += r.candidates.size();
    EXPECT_GE(r.candidates.size(), 2u) << r.county_zh;
    if (r.municipal) {
      ++municipal;
      municipal_candidates += static_cast<int>(r.candidates.size());
    }
    for (const Candidate& c : r.candidates) {
      EXPECT_TRUE(ids.insert(c.id).second) << "duplicate id " << c.id;
      EXPECT_EQ(c.id.substr(0, 5), r.county_code);
      EXPECT_FALSE(c.name_zh.empty());
    }
  }
  // CEC: 81 registrants = 23 for the 6 special municipalities + 58 others.
  EXPECT_EQ(candidates, 81u);
  EXPECT_EQ(municipal, 6);
  EXPECT_EQ(municipal_candidates, 23);
}

TEST_F(ElectionDataTest, EveryPartyIsKnownAndColoured) {
  std::set<std::string> codes;
  for (const Party& p : data_->parties()) {
    codes.insert(p.code);
    EXPECT_NE(p.color & 0x00FFFFFF, 0u) << p.code;
  }
  for (const Race& r : data_->races()) {
    EXPECT_TRUE(codes.count(r.incumbent.party)) << r.incumbent.party;
    for (const Candidate& c : r.candidates) {
      EXPECT_TRUE(codes.count(c.party)) << c.name_zh << " " << c.party;
      for (const std::string& e : c.endorsed_by) EXPECT_TRUE(codes.count(e)) << e;
    }
  }
  for (const Party& p : data_->parties()) {
    EXPECT_FALSE(p.name_ja.empty()) << p.code;
    EXPECT_FALSE(p.short_ja.empty()) << p.code;
    EXPECT_FALSE(p.short_en.empty()) << p.code;
  }
  EXPECT_EQ(data_->party("KMT").name_zh, "中國國民黨");
  EXPECT_EQ(data_->party("KMT").name_ja, "中国国民党");
  EXPECT_EQ(data_->party("DPP").name_zh, "民主進步黨");
}

TEST_F(ElectionDataTest, EveryRaceMapsToACounty) {
  for (const Race& r : data_->races()) {
    const int id = tree_->FindByCode(r.county_code);
    ASSERT_GE(id, 0) << r.county_code;
    EXPECT_EQ(tree_->region(id).name_zh, r.county_zh);
    EXPECT_EQ(data_->RaceForCounty(r.county_code), &r);
  }
}

TEST_F(ElectionDataTest, PhotosReferencedExistOrAreOptional) {
  int present = 0;
  for (const Race& r : data_->races()) {
    for (const Candidate& c : r.candidates) {
      present += std::filesystem::exists(c.photo);
    }
  }
  EXPECT_GE(present, 30);
}

TEST_F(ElectionDataTest, ReferendumAndOffices) {
  ASSERT_EQ(data_->info().referendums.size(), 1u);
  EXPECT_EQ(data_->info().referendums[0].case_no, 22);
  EXPECT_FALSE(data_->info().referendums[0].question_ja.empty());
  EXPECT_FALSE(data_->info().referendums[0].question_en.empty());
  for (const auto& o : data_->info().offices) {
    EXPECT_FALSE(o.name_ja.empty());
    EXPECT_FALSE(o.name_en.empty());
  }
  int seats = 0, cands = 0;
  for (const auto& o : data_->info().offices) {
    seats += o.seats;
    cands += o.candidates;
  }
  EXPECT_EQ(seats, data_->info().total_seats);
  EXPECT_EQ(cands, data_->info().total_candidates);
}

TEST_F(ElectionDataTest, ResultsAggregateUpTheHierarchy) {
  const Race& taipei = *data_->RaceForCounty("63000");
  const int county = tree_->FindByCode("63000");
  const int town_a = tree_->region(county).children[0];
  const int town_b = tree_->region(county).children[1];
  const int village = tree_->region(town_b).children[0];
  // Town A reported as a whole, town B only through one village.
  std::string json = R"({"status": "counting", "races": {"63000-mayor": {"regions": {)";
  json += "\"" + tree_->region(town_a).code +
          R"(": {"votes": {"63000-02": 100, "63000-05": 300}, "eligible": 1000,
                 "ballots_cast": 420, "units_counted": 10, "units_total": 12},)";
  json += "\"" + tree_->region(village).code +
          R"(": {"votes": {"63000-02": 50, "63000-05": 10, "bogus": 99},
                 "units_counted": 1, "units_total": 1}}}}})";
  auto snap = std::make_shared<ResultsSnapshot>();
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, snap.get(), &error)) << error;
  EXPECT_EQ(snap->status, ResultsStatus::kCounting);
  ResultsView view(data_, tree_, snap);
  const Tally& t = view.RaceTally(taipei, county);
  const int shen = taipei.CandidateIndex("63000-02");
  const int chiang = taipei.CandidateIndex("63000-05");
  EXPECT_EQ(t.votes[shen], 150);
  EXPECT_EQ(t.votes[chiang], 310);
  EXPECT_EQ(t.TotalVotes(), 460);
  EXPECT_EQ(t.Leader(), chiang);
  EXPECT_EQ(t.units_counted, 11);
  EXPECT_EQ(t.units_total, 13);
  EXPECT_NEAR(t.Share(chiang), 310.0 / 460.0, 1e-9);
  EXPECT_EQ(view.RaceTally(taipei, town_b).Leader(), shen);
  EXPECT_EQ(view.RaceForRegion(village), &taipei);
}

TEST_F(ElectionDataTest, ResultsJsonRoundTrip) {
  SimulatedResultsSource sim(data_, tree_, 7, 60);
  auto snap = sim.SnapshotAt(0.6);
  const std::string json = ResultsToJson(*snap, *data_);
  auto parsed = std::make_shared<ResultsSnapshot>();
  std::string error;
  ASSERT_TRUE(ParseResultsJson(json, *data_, parsed.get(), &error)) << error;
  ResultsView a(data_, tree_, snap), b(data_, tree_, parsed);
  for (const Race& r : data_->races()) {
    const int county = tree_->FindByCode(r.county_code);
    EXPECT_EQ(a.RaceTally(r, county).votes, b.RaceTally(r, county).votes) << r.id;
  }
  EXPECT_TRUE(parsed->simulated);
}

TEST_F(ElectionDataTest, SimulationIsDeterministicAndMonotonic) {
  SimulatedResultsSource sim(data_, tree_, 42, 60);
  auto early = sim.SnapshotAt(0.3);
  auto late = sim.SnapshotAt(0.9);
  auto again = sim.SnapshotAt(0.9);
  ResultsView ve(data_, tree_, early), vl(data_, tree_, late), va(data_, tree_, again);
  auto done = sim.SnapshotAt(1.0);
  ResultsView vd(data_, tree_, done);
  for (const Race& r : data_->races()) {
    const int county = tree_->FindByCode(r.county_code);
    EXPECT_LE(ve.RaceTally(r, county).units_counted, vl.RaceTally(r, county).units_counted);
    EXPECT_EQ(vl.RaceTally(r, county).votes, va.RaceTally(r, county).votes);
    EXPECT_EQ(vd.RaceTally(r, county).units_counted, vd.RaceTally(r, county).units_total);
  }
  EXPECT_EQ(done->status, ResultsStatus::kFinal);
  EXPECT_TRUE(done->simulated);
}

TEST(RefTally, TaiwanReferendumThreshold) {
  RefTally t;
  t.eligible = 1000;
  t.agree = 260;
  t.disagree = 100;
  EXPECT_TRUE(t.Passes());
  t.agree = 240;  // below 1/4 of the electorate
  EXPECT_FALSE(t.Passes());
  t.agree = 400;
  t.disagree = 500;
  EXPECT_FALSE(t.Passes());
}

TEST(Color, ParsesHex) {
  EXPECT_EQ(ParseColor("#1A48B5", 0), 0xFF1A48B5u);
  EXPECT_EQ(ParseColor("1A48B5", 7), 7u);
  EXPECT_EQ(ParseColor("#GG0000", 7), 7u);
}

}  // namespace
}  // namespace twn::election
