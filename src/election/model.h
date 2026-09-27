// Static election data: parties, races, candidates, referendum, offices.
// Loaded from data/election/2026/{parties,candidates,election}.json.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace twn::election {

struct Party {
  std::string code;  // "KMT", "DPP", "TPP", "IND", ...
  std::string name_zh;
  std::string name_en;
  std::string short_zh;
  std::string name_ja;
  std::string short_ja;
  std::string short_en;
  uint32_t color = 0xFF8C8C8C;  // ARGB
};

struct Candidate {
  std::string id;  // "<county code>-<registration order>", e.g. "63000-05"
  std::string name_zh;
  std::string name_en;
  bool romanization_guess = false;
  std::string party;                     // party code
  std::vector<std::string> endorsed_by;  // party codes backing an independent
  std::optional<int> ballot_no;          // drawn 2026-10-23
  bool incumbent = false;
  std::string photo;  // path relative to the data root
  std::string note;
};

struct Incumbent {
  std::string name_zh;
  std::string party;
  bool term_limited = false;
  std::string note;
};

struct Race {
  std::string id;           // "63000-mayor"
  std::string county_code;  // "63000"
  std::string county_zh;
  std::string county_en;
  std::string office;     // "mayor"
  std::string office_zh;  // "市長" / "縣長"
  bool municipal = false;  // special municipality (直轄市)
  Incumbent incumbent;
  std::vector<Candidate> candidates;

  int CandidateIndex(std::string_view candidate_id) const;
  // "臺北市長", "苗栗縣長": the county name plus 長.
  std::string TitleZh() const { return county_zh + "長"; }
};

struct Referendum {
  std::string id;  // "ref-22"
  int case_no = 0;
  std::string question_zh;
  std::string question_en;
  std::string question_ja;
};

struct OfficeSummary {
  std::string name_zh;
  std::string name_ja;
  std::string name_en;
  int seats = 0;
  int candidates = 0;
  bool headline = false;  // the mayor/magistrate races this app visualises
};

struct ElectionInfo {
  std::string id;
  std::string date;  // YYYY-MM-DD
  std::string name_zh;
  std::string name_en;
  std::string polls_open;
  std::string polls_close;
  std::string status_note;
  std::vector<OfficeSummary> offices;
  int total_seats = 0;
  int total_candidates = 0;
  std::vector<Referendum> referendums;
};

class ElectionData {
 public:
  // Loads the three JSON files from `dir` (e.g. "data/election/2026").
  bool Load(const std::string& dir, std::string* error);

  // Same, from in-memory JSON text (used by tests).
  bool LoadFromJson(std::string_view parties_json, std::string_view candidates_json,
                    std::string_view election_json, std::string* error);

  const ElectionInfo& info() const { return info_; }
  const std::vector<Party>& parties() const { return parties_; }
  const std::vector<Race>& races() const { return races_; }

  const Party& party(std::string_view code) const;  // falls back to IND
  const Race* RaceForCounty(std::string_view county_code) const;
  const Race* RaceById(std::string_view id) const;
  const Candidate* CandidateById(std::string_view id, const Race** race = nullptr) const;

 private:
  ElectionInfo info_;
  std::vector<Party> parties_;
  std::vector<Race> races_;
  Party unknown_party_;
};

bool ReadFile(const std::string& path, std::string* out);

// Parses "#RRGGBB" to 0xFFRRGGBB.
uint32_t ParseColor(std::string_view hex, uint32_t fallback);

}  // namespace twn::election
