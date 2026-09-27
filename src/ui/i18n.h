// UI localisation: Traditional Chinese (default), Japanese, English.
//
// UI strings live in a key -> {zh-TW, ja, en} table (i18n.cc). Data names
// come from the data files: Chinese names as published, Japanese forms
// derived by mapping Traditional characters to Japanese shinjitai
// (臺北市 -> 台北市, 苗栗縣 -> 苗栗県, 大安區 -> 大安区), and English names from the
// atlas / candidate romanisations.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "src/election/model.h"
#include "src/geo/region_tree.h"

namespace twn::ui {

enum class Lang { kZhTW = 0, kJa = 1, kEn = 2, kCount = 3 };

// Accepts "zh", "zh-TW", "zh_TW", "tw", "ja", "jp", "en" (case-insensitive).
bool ParseLang(std::string_view s, Lang* out);
const char* LangCode(Lang l);        // "zh-TW", "ja", "en"
const char* LangNativeName(Lang l);  // "繁體中文", "日本語", "English"
Lang NextLang(Lang l);

// Looks up a UI string; falls back to Traditional Chinese, then the key.
const char* Tr(Lang lang, std::string_view key);

// Substitutes {0}, {1}, ... in `pattern`.
std::string Fmt(std::string_view pattern, std::initializer_list<std::string> args);

// Maps Traditional Chinese characters to their Japanese (shinjitai) forms.
std::string ToJapaneseKanji(std::string_view zh);

class Localizer {
 public:
  explicit Localizer(Lang lang = Lang::kZhTW) : lang_(lang) {}
  Lang lang() const { return lang_; }
  const char* T(std::string_view key) const { return Tr(lang_, key); }

  std::string RegionName(const geo::Region& r) const;
  std::string CandidateName(const election::Candidate& c) const;
  // Secondary line under the name (the other script).
  std::string CandidateAltName(const election::Candidate& c) const;
  std::string PartyShort(const election::Party& p) const;
  std::string PartyName(const election::Party& p) const;
  // "臺北市長" / "台北市長" / "Taipei City Mayor".
  std::string RaceTitle(const election::Race& r) const;
  std::string OfficeName(const election::OfficeSummary& o) const;
  std::string Question(const election::Referendum& r) const;
  std::string ListSeparator() const;  // "、" or ", "

 private:
  Lang lang_;
};

}  // namespace twn::ui
