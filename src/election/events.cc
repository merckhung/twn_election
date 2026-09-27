#include "src/election/events.h"

#include <algorithm>
#include <cstdio>

namespace twn::election {

bool IsBreaking(EventType t) {
  switch (t) {
    case EventType::kLeadChange:
    case EventType::kVictoryDeclared:
    case EventType::kConcession:
    case EventType::kCalled:
    case EventType::kIncumbentTrailing:
      return true;
    default:
      return false;
  }
}

const char* EventKey(EventType t) {
  switch (t) {
    case EventType::kFirstReturns: return "first";
    case EventType::kLeadChange: return "lead_change";
    case EventType::kVictoryDeclared: return "victory";
    case EventType::kConcession: return "concede";
    case EventType::kCalled: return "called";
    case EventType::kIncumbentTrailing: return "incumbent_trailing";
    case EventType::kCloseRace: return "close";
    case EventType::kFinal: return "final";
  }
  return "";
}

std::string TimeLabel(const std::string& updated_at) {
  const size_t t = updated_at.find('T');
  if (t == std::string::npos || updated_at.size() < t + 6) return "";
  return updated_at.substr(t + 1, 5);
}

double MinutesAfterClose(const std::string& updated_at) {
  int h = 0, m = 0;
  const std::string label = TimeLabel(updated_at);
  if (label.empty() || std::sscanf(label.c_str(), "%d:%d", &h, &m) != 2) return -1;
  return (h - 16) * 60.0 + m;
}

bool EventTracker::Decided(const Tally& t) {
  if (t.units_total <= 0 || t.units_counted <= 0) return false;
  const std::vector<int> rank = t.Ranking();
  if (rank.size() < 2) return t.units_counted >= t.units_total;
  const int64_t margin = t.votes[rank[0]] - t.votes[rank[1]];
  if (margin <= 0) return false;
  if (t.units_counted >= t.units_total) return true;
  // Require a meaningful share of the count before projecting.
  if (t.Progress() < 0.25) return false;
  const double per_unit = static_cast<double>(t.TotalVotes()) / t.units_counted;
  const double remaining = per_unit * (t.units_total - t.units_counted) * 1.15;
  return static_cast<double>(margin) > remaining;
}

bool EventTracker::IsCalled(const std::string& race_id) const {
  auto it = races_.find(race_id);
  return it != races_.end() && it->second.called;
}

void EventTracker::Reset() {
  has_baseline_ = false;
  regions_.clear();
  races_.clear();
}

EventBatch EventTracker::Update(ResultsView& view, const std::vector<Race>& races) {
  EventBatch batch;
  batch.baseline = !has_baseline_;
  const std::string time = TimeLabel(view.snapshot().updated_at);

  for (const Race& race : races) {
    const int county = tree_->FindByCode(race.county_code);
    if (county < 0) continue;
    // County + towns + villages of this race.
    std::vector<int> regions = {county};
    for (int town : tree_->region(county).children) {
      regions.push_back(town);
      for (int v : tree_->region(town).children) regions.push_back(v);
    }
    for (int r : regions) {
      const Tally& t = view.RaceTally(race, r);
      const int64_t total = t.TotalVotes();
      const int leader = total > 0 ? t.Leader() : -1;
      RegionState& prev = regions_[r];
      if (!batch.baseline) {
        if (total > prev.total) {
          batch.deltas.push_back({r, total - prev.total});
          if (r == county) batch.votes_added += total - prev.total;
        }
        if (prev.leader >= 0 && leader >= 0 && leader != prev.leader) {
          batch.flips.push_back({r, &race, leader, prev.leader});
        }
      }
      const int64_t prev_total = prev.total;
      prev.total = total;
      prev.leader = leader;

      if (r != county) continue;
      RaceState& rs = races_[race.id];
      const std::vector<int> rank = t.Ranking();
      const int64_t margin =
          total > 0 && rank.size() > 1 ? t.votes[rank[0]] - t.votes[rank[1]] : total;
      const double progress = t.Progress();
      auto emit = [&](EventType type, int who, int other) {
        ElectionEvent e;
        e.type = type;
        e.race = &race;
        e.leader = who;
        e.previous = other;
        e.margin = margin;
        e.progress = progress;
        e.time_label = time;
        batch.events.push_back(e);
        return &batch.events.back();
      };
      int incumbent = -1;
      for (size_t i = 0; i < race.candidates.size(); ++i) {
        if (race.candidates[i].incumbent) incumbent = static_cast<int>(i);
      }
      // Declarations (from the feed) seen so far for this race.
      std::vector<const Declaration*> decls;
      for (const Declaration& d : view.snapshot().declarations) {
        if (d.race_id == race.id) decls.push_back(&d);
      }
      if (batch.baseline) {
        rs.called = Decided(t);
        rs.final = t.units_total > 0 && t.units_counted >= t.units_total;
        rs.close_alerted = true;
        rs.incumbent_alerted = incumbent >= 0 && leader >= 0 && leader != incumbent;
        rs.declarations_seen = decls.size();
        rs.leader = leader;
        for (const Declaration* d : decls) {
          if (d->type == Declaration::Type::kVictory) rs.declared = race.CandidateIndex(d->candidate_id);
        }
        continue;
      }
      if (prev_total == 0 && total > 0) {
        emit(EventType::kFirstReturns, leader, -1);
        rs.leader = leader;
      }
      // Lead changes need a small margin (0.1% of votes, at least 30) so a
      // neck-and-neck race does not flip-flop with every polling station.
      if (rs.leader >= 0 && leader >= 0 && leader != rs.leader &&
          margin >= std::max<int64_t>(30, total / 1000)) {
        ElectionEvent* e = emit(EventType::kLeadChange, leader, rs.leader);
        e->after_declaration = rs.declared == rs.leader;
        rs.leader = leader;
      }
      for (size_t k = rs.declarations_seen; k < decls.size(); ++k) {
        const int who = race.CandidateIndex(decls[k]->candidate_id);
        if (decls[k]->type == Declaration::Type::kVictory) {
          rs.declared = who;
          ElectionEvent* e = emit(EventType::kVictoryDeclared, who, -1);
          if (!TimeLabel(decls[k]->time).empty()) e->time_label = TimeLabel(decls[k]->time);
        } else {
          ElectionEvent* e = emit(EventType::kConcession, who, leader);
          if (!TimeLabel(decls[k]->time).empty()) e->time_label = TimeLabel(decls[k]->time);
        }
      }
      rs.declarations_seen = decls.size();
      if (!rs.incumbent_alerted && incumbent >= 0 && leader >= 0 && leader != incumbent &&
          progress >= 0.3) {
        rs.incumbent_alerted = true;
        emit(EventType::kIncumbentTrailing, leader, incumbent);
      }
      if (!rs.close_alerted && progress >= 0.7 && total > 0 && margin * 100 < total) {
        rs.close_alerted = true;
        emit(EventType::kCloseRace, leader, rank.size() > 1 ? rank[1] : -1);
      }
      if (!rs.called && Decided(t)) {
        rs.called = true;
        emit(EventType::kCalled, leader, -1);
      }
      if (!rs.final && t.units_total > 0 && t.units_counted >= t.units_total) {
        rs.final = true;
        emit(EventType::kFinal, leader, -1);
      }
    }
  }
  has_baseline_ = true;
  return batch;
}

}  // namespace twn::election
