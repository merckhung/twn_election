// results_tool: helpers for producing results files (twn_election.results/v1).
//
//   bazel run //tools:results_tool -- --template > feed.json
//       Zeroed county-level entries for every race/candidate (a starting point
//       for a feed adapter).
//   bazel run //tools:results_tool -- --simulate=0.6 > sim.json
//       A full village-level SIMULATED snapshot (flagged "simulated": true).
//   bazel run //tools:results_tool -- --check=feed.json
//       Validates a results file and prints per-race county totals.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "src/election/model.h"
#include "src/election/results.h"
#include "src/election/results_source.h"
#include "src/geo/region_tree.h"

using namespace twn;

int main(int argc, char** argv) {
  std::string root = ".";
  if (const char* ws = std::getenv("BUILD_WORKSPACE_DIRECTORY")) root = ws;
  std::string mode, arg;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("--root=", 0) == 0) root = a.substr(7);
    else if (a == "--template") mode = "template";
    else if (a.rfind("--simulate=", 0) == 0) mode = "simulate", arg = a.substr(11);
    else if (a.rfind("--check=", 0) == 0) mode = "check", arg = a.substr(8);
  }
  if (mode.empty()) {
    std::fprintf(stderr, "usage: results_tool [--root=DIR] --template | --simulate=P | --check=FILE\n");
    return 2;
  }
  election::ElectionData data;
  std::string error;
  if (!data.Load(root + "/data/election/2026", &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  geo::RegionTree tree;
  std::string topo;
  if (!election::ReadFile(root + "/data/map/villages-10t.json", &topo) ||
      !tree.LoadTopoJson(topo, &error)) {
    std::fprintf(stderr, "cannot load map: %s\n", error.c_str());
    return 1;
  }

  if (mode == "template") {
    election::ResultsSnapshot snap;
    snap.status = election::ResultsStatus::kPreElection;
    snap.source = "template";
    for (const election::Race& race : data.races()) {
      election::Tally t;
      t.votes.assign(race.candidates.size(), 0);
      snap.races[race.id][race.county_code] = t;
    }
    for (const auto& ref : data.info().referendums) snap.referendums[ref.id]["TW"] = {};
    std::cout << election::ResultsToJson(snap, data) << "\n";
    return 0;
  }
  if (mode == "simulate") {
    election::SimulatedResultsSource sim(&data, &tree, 20261128, 60);
    auto snap = sim.SnapshotAt(std::atof(arg.c_str()));
    std::cout << election::ResultsToJson(*snap, data) << "\n";
    return 0;
  }
  std::string text;
  if (!election::ReadFile(arg, &text)) {
    std::fprintf(stderr, "cannot read %s\n", arg.c_str());
    return 1;
  }
  auto snap = std::make_shared<election::ResultsSnapshot>();
  if (!election::ParseResultsJson(text, data, snap.get(), &error)) {
    std::fprintf(stderr, "invalid: %s\n", error.c_str());
    return 1;
  }
  election::ResultsView view(&data, &tree, snap);
  std::printf("status=%s simulated=%d source=%s\n", election::StatusLabelZh(snap->status),
              snap->simulated, snap->source.c_str());
  for (const election::Race& race : data.races()) {
    const election::Tally& t = view.RaceTally(race, tree.FindByCode(race.county_code));
    const int lead = t.Leader();
    std::printf("%-8s units %d/%d votes %lld leader %s\n", race.id.c_str(), t.units_counted,
                t.units_total, static_cast<long long>(t.TotalVotes()),
                lead >= 0 ? race.candidates[lead].name_zh.c_str() : "-");
  }
  return 0;
}
