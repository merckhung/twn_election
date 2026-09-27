#include "src/election/results.h"

#include <algorithm>
#include <numeric>

#include "nlohmann/json.hpp"

namespace twn::election {

using nlohmann::json;

int64_t Tally::TotalVotes() const { return std::accumulate(votes.begin(), votes.end(), int64_t{0}); }

int Tally::Leader() const {
  int best = -1;
  int64_t best_votes = 0;
  for (size_t i = 0; i < votes.size(); ++i) {
    if (votes[i] > best_votes) {
      best_votes = votes[i];
      best = static_cast<int>(i);
    }
  }
  return best;
}

std::vector<int> Tally::Ranking() const {
  std::vector<int> order(votes.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return votes[a] > votes[b]; });
  return order;
}

double Tally::Share(int candidate) const {
  const int64_t total = TotalVotes();
  if (total <= 0 || candidate < 0 || candidate >= static_cast<int>(votes.size())) return 0;
  return static_cast<double>(votes[candidate]) / static_cast<double>(total);
}

double Tally::Turnout() const {
  return eligible > 0 ? static_cast<double>(ballots_cast) / static_cast<double>(eligible) : 0;
}

double Tally::Progress() const {
  return units_total > 0 ? static_cast<double>(units_counted) / units_total : 0;
}

void Tally::Add(const Tally& o) {
  if (votes.size() < o.votes.size()) votes.resize(o.votes.size(), 0);
  for (size_t i = 0; i < o.votes.size(); ++i) votes[i] += o.votes[i];
  eligible += o.eligible;
  ballots_cast += o.ballots_cast;
  units_counted += o.units_counted;
  units_total += o.units_total;
  has_data = has_data || o.has_data;
}

void RefTally::Add(const RefTally& o) {
  agree += o.agree;
  disagree += o.disagree;
  eligible += o.eligible;
  ballots_cast += o.ballots_cast;
  units_counted += o.units_counted;
  units_total += o.units_total;
  has_data = has_data || o.has_data;
}

double RefTally::Progress() const {
  return units_total > 0 ? static_cast<double>(units_counted) / units_total : 0;
}

bool RefTally::Passes() const { return agree > disagree && eligible > 0 && agree * 4 >= eligible; }

const char* StatusLabelZh(ResultsStatus s) {
  switch (s) {
    case ResultsStatus::kPreElection: return "尚未開票";
    case ResultsStatus::kCounting: return "開票中";
    case ResultsStatus::kFinal: return "開票完成";
  }
  return "";
}

bool ParseResultsJson(std::string_view text, const ElectionData& data, ResultsSnapshot* out,
                      std::string* error) {
  const json doc = json::parse(text, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    *error = "invalid results JSON";
    return false;
  }
  try {
    const std::string status = doc.value("status", "pre-election");
    out->status = status == "final"      ? ResultsStatus::kFinal
                  : status == "counting" ? ResultsStatus::kCounting
                                         : ResultsStatus::kPreElection;
    out->source = doc.value("source", "");
    out->updated_at = doc.value("updated_at", "");
    out->simulated = doc.value("simulated", false);
    out->races.clear();
    out->referendums.clear();
    if (doc.contains("races")) {
      for (const auto& [race_id, race_json] : doc["races"].items()) {
        const Race* race = data.RaceById(race_id);
        if (!race) continue;  // unknown race: ignore (forward compatible)
        auto& regions = out->races[race_id];
        // Note: bind json::value() results to locals; iterating .items() of a
        // temporary would dangle.
        const json region_map = race_json.value("regions", json::object());
        for (const auto& [code, t] : region_map.items()) {
          Tally tally;
          tally.votes.assign(race->candidates.size(), 0);
          const json votes = t.value("votes", json::object());
          for (const auto& [cid, v] : votes.items()) {
            const int idx = race->CandidateIndex(cid);
            if (idx >= 0) tally.votes[idx] = v.get<int64_t>();
          }
          tally.eligible = t.value("eligible", int64_t{0});
          tally.ballots_cast = t.value("ballots_cast", int64_t{0});
          tally.units_counted = t.value("units_counted", 0);
          tally.units_total = t.value("units_total", 0);
          tally.has_data = true;
          regions[code] = std::move(tally);
        }
      }
    }
    out->declarations.clear();
    for (const json& d : doc.value("declarations", json::array())) {
      Declaration decl;
      decl.race_id = d.value("race", "");
      decl.candidate_id = d.value("candidate", "");
      decl.type = d.value("type", "victory") == "concede" ? Declaration::Type::kConcede
                                                          : Declaration::Type::kVictory;
      decl.time = d.value("time", "");
      const Race* race = data.RaceById(decl.race_id);
      if (race && race->CandidateIndex(decl.candidate_id) >= 0) {
        out->declarations.push_back(std::move(decl));
      }
    }
    if (doc.contains("referendums")) {
      for (const auto& [ref_id, ref_json] : doc["referendums"].items()) {
        auto& regions = out->referendums[ref_id];
        const json region_map = ref_json.value("regions", json::object());
        for (const auto& [code, t] : region_map.items()) {
          RefTally r;
          r.agree = t.value("agree", int64_t{0});
          r.disagree = t.value("disagree", int64_t{0});
          r.eligible = t.value("eligible", int64_t{0});
          r.ballots_cast = t.value("ballots_cast", int64_t{0});
          r.units_counted = t.value("units_counted", 0);
          r.units_total = t.value("units_total", 0);
          r.has_data = true;
          regions[code] = r;
        }
      }
    }
  } catch (const json::exception& ex) {
    *error = std::string("results JSON schema error: ") + ex.what();
    return false;
  }
  return true;
}

std::string ResultsToJson(const ResultsSnapshot& snap, const ElectionData& data) {
  json doc;
  doc["schema"] = "twn_election.results/v1";
  doc["status"] = snap.status == ResultsStatus::kFinal      ? "final"
                  : snap.status == ResultsStatus::kCounting ? "counting"
                                                            : "pre-election";
  doc["source"] = snap.source;
  doc["updated_at"] = snap.updated_at;
  doc["simulated"] = snap.simulated;
  json races = json::object();
  for (const auto& [race_id, regions] : snap.races) {
    const Race* race = data.RaceById(race_id);
    if (!race) continue;
    json rj = json::object();
    for (const auto& [code, t] : regions) {
      json tj;
      json votes = json::object();
      for (size_t i = 0; i < t.votes.size() && i < race->candidates.size(); ++i) {
        votes[race->candidates[i].id] = t.votes[i];
      }
      tj["votes"] = votes;
      tj["eligible"] = t.eligible;
      tj["ballots_cast"] = t.ballots_cast;
      tj["units_counted"] = t.units_counted;
      tj["units_total"] = t.units_total;
      rj[code] = tj;
    }
    races[race_id]["regions"] = rj;
  }
  doc["races"] = races;
  json refs = json::object();
  for (const auto& [ref_id, regions] : snap.referendums) {
    json rj = json::object();
    for (const auto& [code, t] : regions) {
      rj[code] = {{"agree", t.agree},           {"disagree", t.disagree},
                  {"eligible", t.eligible},     {"ballots_cast", t.ballots_cast},
                  {"units_counted", t.units_counted}, {"units_total", t.units_total}};
    }
    refs[ref_id]["regions"] = rj;
  }
  doc["referendums"] = refs;
  json decls = json::array();
  for (const Declaration& d : snap.declarations) {
    decls.push_back({{"race", d.race_id},
                     {"candidate", d.candidate_id},
                     {"type", d.type == Declaration::Type::kConcede ? "concede" : "victory"},
                     {"time", d.time}});
  }
  doc["declarations"] = decls;
  return doc.dump(1);
}

ResultsView::ResultsView(const ElectionData* data, const geo::RegionTree* tree,
                         std::shared_ptr<const ResultsSnapshot> snapshot)
    : data_(data), tree_(tree), snapshot_(std::move(snapshot)) {}

const Tally& ResultsView::RaceTally(const Race& race, int region_id) {
  auto& memo = race_memo_[race.id];
  if (auto it = memo.find(region_id); it != memo.end()) return it->second;

  Tally tally;
  tally.votes.assign(race.candidates.size(), 0);
  const geo::Region& region = tree_->region(region_id);
  const auto race_it = snapshot_->races.find(race.id);
  bool explicit_entry = false;
  if (race_it != snapshot_->races.end()) {
    if (auto t = race_it->second.find(region.code); t != race_it->second.end()) {
      tally = t->second;
      tally.votes.resize(race.candidates.size(), 0);
      explicit_entry = true;
    }
  }
  if (!explicit_entry) {
    for (int child : region.children) {
      const geo::Region& c = tree_->region(child);
      if (!c.county_code.empty() && c.county_code != race.county_code) continue;
      tally.Add(RaceTally(race, child));
    }
  }
  return memo.emplace(region_id, std::move(tally)).first->second;
}

const RefTally& ResultsView::ReferendumTally(const std::string& ref_id, int region_id) {
  auto& memo = ref_memo_[ref_id];
  if (auto it = memo.find(region_id); it != memo.end()) return it->second;
  RefTally tally;
  const geo::Region& region = tree_->region(region_id);
  const auto ref_it = snapshot_->referendums.find(ref_id);
  bool explicit_entry = false;
  if (ref_it != snapshot_->referendums.end()) {
    if (auto t = ref_it->second.find(region.code); t != ref_it->second.end()) {
      tally = t->second;
      explicit_entry = true;
    }
  }
  if (!explicit_entry) {
    for (int child : region.children) tally.Add(ReferendumTally(ref_id, child));
  }
  return memo.emplace(region_id, tally).first->second;
}

const Race* ResultsView::RaceForRegion(int region_id) const {
  const geo::Region& r = tree_->region(region_id);
  if (r.county_code.empty()) return nullptr;
  return data_->RaceForCounty(r.county_code);
}

}  // namespace twn::election
