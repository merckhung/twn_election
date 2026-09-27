#include "src/election/model.h"

#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

namespace twn::election {

using nlohmann::json;

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

uint32_t ParseColor(std::string_view hex, uint32_t fallback) {
  if (hex.size() != 7 || hex[0] != '#') return fallback;
  uint32_t v = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = hex[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= c - '0';
    else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    else return fallback;
  }
  return 0xFF000000u | v;
}

int Race::CandidateIndex(std::string_view candidate_id) const {
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (candidates[i].id == candidate_id) return static_cast<int>(i);
  }
  return -1;
}

bool ElectionData::Load(const std::string& dir, std::string* error) {
  std::string parties, candidates, election;
  for (auto [name, dst] : {std::pair{"parties.json", &parties},
                           std::pair{"candidates.json", &candidates},
                           std::pair{"election.json", &election}}) {
    if (!ReadFile(dir + "/" + name, dst)) {
      *error = "cannot read " + dir + "/" + name;
      return false;
    }
  }
  return LoadFromJson(parties, candidates, election, error);
}

bool ElectionData::LoadFromJson(std::string_view parties_json, std::string_view candidates_json,
                                std::string_view election_json, std::string* error) {
  const json pj = json::parse(parties_json, nullptr, false);
  const json cj = json::parse(candidates_json, nullptr, false);
  const json ej = json::parse(election_json, nullptr, false);
  if (pj.is_discarded() || cj.is_discarded() || ej.is_discarded()) {
    *error = "invalid election JSON";
    return false;
  }
  try {
    parties_.clear();
    for (const json& p : pj.at("parties")) {
      Party party;
      party.code = p.at("code").get<std::string>();
      party.name_zh = p.value("name_zh", party.code);
      party.name_en = p.value("name_en", party.code);
      party.short_zh = p.value("short_zh", party.name_zh);
      party.name_ja = p.value("name_ja", party.name_zh);
      party.short_ja = p.value("short_ja", party.short_zh);
      party.short_en = p.value("short_en", party.code);
      party.color = ParseColor(p.value("color", ""), 0xFF8C8C8C);
      parties_.push_back(std::move(party));
    }
    unknown_party_ = Party{"IND", "無黨籍", "Independent", "無黨籍", "無所属", "無所属", "Ind.",
                           0xFF8C8C8C};

    const json& e = cj.at("election");
    info_.id = e.value("id", "");
    info_.date = e.value("date", "");
    info_.name_zh = e.value("name_zh", "");
    info_.name_en = e.value("name_en", "");
    info_.polls_open = e.value("polls_open", "08:00");
    info_.polls_close = e.value("polls_close", "16:00");
    info_.status_note = cj.value("status_note", "");

    races_.clear();
    for (const json& r : cj.at("races")) {
      Race race;
      race.id = r.at("id").get<std::string>();
      race.county_code = r.at("county_code").get<std::string>();
      race.county_zh = r.value("county_zh", "");
      race.county_en = r.value("county_en", "");
      race.office = r.value("office", "mayor");
      race.office_zh = r.value("office_zh", "");
      race.municipal = r.value("municipal", false);
      if (r.contains("incumbent")) {
        const json& inc = r["incumbent"];
        race.incumbent.name_zh = inc.value("name_zh", "");
        race.incumbent.party = inc.value("party", "");
        race.incumbent.term_limited = inc.value("term_limited", false);
        race.incumbent.note = inc.value("note", "");
      }
      for (const json& c : r.at("candidates")) {
        Candidate cand;
        cand.id = c.at("id").get<std::string>();
        cand.name_zh = c.value("name_zh", "");
        cand.name_en = c.value("name_en", "");
        cand.romanization_guess = c.value("romanization_guess", false);
        cand.party = c.value("party", "IND");
        if (c.contains("endorsed_by")) {
          cand.endorsed_by = c["endorsed_by"].get<std::vector<std::string>>();
        }
        if (c.contains("ballot_no") && c["ballot_no"].is_number_integer()) {
          cand.ballot_no = c["ballot_no"].get<int>();
        }
        cand.incumbent = c.value("incumbent", false);
        cand.photo = c.value("photo", "");
        cand.note = c.value("note", "");
        race.candidates.push_back(std::move(cand));
      }
      races_.push_back(std::move(race));
    }

    info_.offices.clear();
    for (const json& o : ej.value("offices", json::array())) {
      OfficeSummary office;
      office.name_zh = o.value("name_zh", "");
      office.name_ja = o.value("name_ja", office.name_zh);
      office.name_en = o.value("name_en", office.name_zh);
      office.seats = o.value("seats", 0);
      office.candidates = o.value("candidates", 0);
      office.headline = o.value("headline", false);
      info_.offices.push_back(std::move(office));
    }
    if (ej.contains("totals")) {
      info_.total_seats = ej["totals"].value("seats", 0);
      info_.total_candidates = ej["totals"].value("candidates", 0);
    }
    info_.referendums.clear();
    for (const json& r : ej.value("referendums", json::array())) {
      info_.referendums.push_back({r.value("id", ""), r.value("case_no", 0),
                                   r.value("question_zh", ""), r.value("question_en", ""),
                                   r.value("question_ja", "")});
    }
  } catch (const json::exception& ex) {
    *error = std::string("election JSON schema error: ") + ex.what();
    return false;
  }
  return true;
}

const Party& ElectionData::party(std::string_view code) const {
  for (const Party& p : parties_) {
    if (p.code == code) return p;
  }
  return unknown_party_;
}

const Race* ElectionData::RaceForCounty(std::string_view county_code) const {
  for (const Race& r : races_) {
    if (r.county_code == county_code) return &r;
  }
  return nullptr;
}

const Race* ElectionData::RaceById(std::string_view id) const {
  for (const Race& r : races_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

const Candidate* ElectionData::CandidateById(std::string_view id, const Race** race) const {
  for (const Race& r : races_) {
    for (const Candidate& c : r.candidates) {
      if (c.id == id) {
        if (race) *race = &r;
        return &c;
      }
    }
  }
  return nullptr;
}

}  // namespace twn::election
