#include <set>
#include <string>

#include "gtest/gtest.h"
#include "src/ui/i18n.h"

namespace twn::ui {
namespace {

TEST(I18n, DefaultsToTraditionalChinese) {
  Localizer l;
  EXPECT_EQ(l.lang(), Lang::kZhTW);
  EXPECT_STREQ(l.T("status.counting"), "開票中");
}

TEST(I18n, ParsesLanguageFlags) {
  Lang l = Lang::kZhTW;
  ASSERT_TRUE(ParseLang("ja", &l));
  EXPECT_EQ(l, Lang::kJa);
  ASSERT_TRUE(ParseLang("EN", &l));
  EXPECT_EQ(l, Lang::kEn);
  ASSERT_TRUE(ParseLang("zh-TW", &l));
  EXPECT_EQ(l, Lang::kZhTW);
  EXPECT_FALSE(ParseLang("fr", &l));
  EXPECT_EQ(NextLang(Lang::kZhTW), Lang::kJa);
  EXPECT_EQ(NextLang(Lang::kJa), Lang::kEn);
  EXPECT_EQ(NextLang(Lang::kEn), Lang::kZhTW);
}

TEST(I18n, EveryKeyTranslatedInAllLanguages) {
  const char* keys[] = {"app.title", "status.countdown", "race.title", "nation.title",
                        "footer.controls", "help.9", "ref.threshold", "level.village"};
  for (const char* k : keys) {
    std::set<std::string> distinct;
    for (Lang l : {Lang::kZhTW, Lang::kJa, Lang::kEn}) {
      const std::string s = Tr(l, k);
      EXPECT_NE(s, k) << "missing key " << k;
      distinct.insert(s);
    }
    EXPECT_GE(distinct.size(), 2u) << k;  // at least EN differs from CJK
  }
  EXPECT_STREQ(Tr(Lang::kEn, "no.such.key"), "no.such.key");
}

TEST(I18n, Formats) {
  EXPECT_EQ(Fmt("距投票日 {0} 天", {"62"}), "距投票日 62 天");
  EXPECT_EQ(Fmt("{1}/{0}", {"a", "b"}), "b/a");
  EXPECT_EQ(Fmt("{5}", {"a"}), "");
}

TEST(I18n, JapaneseKanjiForms) {
  EXPECT_EQ(ToJapaneseKanji("臺北市"), "台北市");
  EXPECT_EQ(ToJapaneseKanji("苗栗縣"), "苗栗県");
  EXPECT_EQ(ToJapaneseKanji("大安區"), "大安区");
  EXPECT_EQ(ToJapaneseKanji("蔣萬安"), "蒋万安");
  EXPECT_EQ(ToJapaneseKanji("abc 花蓮"), "abc 花蓮");
}

TEST(I18n, LocalizedNames) {
  election::Race race;
  race.county_zh = "苗栗縣";
  race.county_en = "Miaoli County";
  race.office_zh = "縣長";
  EXPECT_EQ(Localizer(Lang::kZhTW).RaceTitle(race), "苗栗縣長");
  EXPECT_EQ(Localizer(Lang::kJa).RaceTitle(race), "苗栗県長");
  EXPECT_EQ(Localizer(Lang::kEn).RaceTitle(race), "Miaoli County Magistrate");
  election::Candidate c;
  c.name_zh = "蔣萬安";
  c.name_en = "Chiang Wan-an";
  EXPECT_EQ(Localizer(Lang::kEn).CandidateName(c), "Chiang Wan-an");
  EXPECT_EQ(Localizer(Lang::kEn).CandidateAltName(c), "蔣萬安");
  EXPECT_EQ(Localizer(Lang::kJa).CandidateName(c), "蒋万安");
  geo::Region r;
  r.level = geo::Level::kNation;
  r.name_zh = "臺灣";
  r.name_en = "Taiwan";
  EXPECT_EQ(Localizer(Lang::kJa).RegionName(r), "台湾");
  EXPECT_EQ(Localizer(Lang::kEn).RegionName(r), "Taiwan");
}

}  // namespace
}  // namespace twn::ui
