#include "src/news/mock_news.h"

#include <cmath>
#include <cstdio>

namespace twn::news {
namespace {

uint64_t Mix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

double Unit(uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

struct Template {
  store::Sentiment sentiment;
  const char* title;
  const char* body;
};

// {0} = candidate, {1} = race title, {2} = rival.
const Template kTemplates[] = {
    {store::Sentiment::kGood, "{0}造勢晚會湧入大批支持者，現場氣氛熱烈",
     "{1}候選人{0}昨晚舉辦大型造勢活動，主辦單位表示人潮超出預期，{0}感謝支持者力挺。"},
    {store::Sentiment::kGood, "地方人士表態力挺{0}，選情加溫",
     "多位地方人士與里長公開表態支持{1}候選人{0}，陣營認為聲勢看好。"},
    {store::Sentiment::kGood, "最新民調：{0}支持度上升",
     "一份最新民調顯示，{1}選舉中{0}的支持度較上月上升，陣營表示將持續努力。"},
    {store::Sentiment::kGood, "{0}交通政見獲專家肯定",
     "{0}公布的交通建設政見獲多位學者肯定，認為具體可行。"},
    {store::Sentiment::kBad, "{0}政見遭{2}陣營質疑跳票",
     "{1}選戰升溫，{2}陣營今日召開記者會，質疑{0}過去的政見跳票，{0}陣營回應將提出說明。"},
    {store::Sentiment::kBad, "最新民調：{0}支持度下滑",
     "一份最新民調顯示，{1}選舉中{0}的支持度較上月下滑，陣營坦言選情吃緊。"},
    {store::Sentiment::kBad, "{0}辯論會表現遭批評，陣營緊急滅火",
     "{1}電視辯論會後，{0}的表現遭網友與評論者批評，陣營表示將加強說明政策。"},
    {store::Sentiment::kBad, "{0}造勢活動人潮不如預期",
     "{0}週末的造勢活動人潮不如預期，陣營低調回應天候不佳影響。"},
    {store::Sentiment::kNeutral, "{0}今日前往市場拜票",
     "{1}候選人{0}今日前往傳統市場拜票，與攤商及民眾握手寒暄。"},
    {store::Sentiment::kNeutral, "{0}與{2}出席同一場廟會活動",
     "{1}候選人{0}與{2}今日出席同一場廟會活動，兩人互動有禮。"},
    {store::Sentiment::kNeutral, "{0}發布競選廣告，主打城市願景",
     "{0}陣營今日發布新一支競選廣告，內容介紹{1}的城市願景。"},
};

std::string Fill(const char* pattern, const std::string& a, const std::string& b,
                 const std::string& c) {
  std::string out;
  for (const char* p = pattern; *p; ++p) {
    if (p[0] == '{' && p[1] >= '0' && p[1] <= '2' && p[2] == '}') {
      out += p[1] == '0' ? a : p[1] == '1' ? b : c;
      p += 2;
    } else {
      out += *p;
    }
  }
  return out;
}

}  // namespace

MockNewsGenerator::MockNewsGenerator(const election::ElectionData* data, uint64_t seed)
    : data_(data), seed_(seed) {
  // Main candidates get more coverage: weight by party size and incumbency.
  const auto& races = data_->races();
  for (size_t r = 0; r < races.size(); ++r) {
    for (size_t c = 0; c < races[r].candidates.size(); ++c) {
      const auto& cand = races[r].candidates[c];
      int weight = 1;
      if (cand.party == "KMT" || cand.party == "DPP" || cand.party == "TPP") weight += 3;
      if (cand.incumbent || !cand.endorsed_by.empty()) weight += 2;
      if (races[r].municipal) weight += 1;
      for (int w = 0; w < weight; ++w) pool_.push_back({static_cast<int>(r), static_cast<int>(c)});
    }
  }
}

MockArticle MockNewsGenerator::Make(uint64_t h, double minute) {
  const auto& races = data_->races();
  const auto [ri, ci] = pool_[Mix(h + 1) % pool_.size()];
  const election::Race& race = races[ri];
  const election::Candidate& cand = race.candidates[ci];
  int rival = static_cast<int>(Mix(h + 2) % race.candidates.size());
  if (rival == ci) rival = (rival + 1) % static_cast<int>(race.candidates.size());
  const Template& t = kTemplates[Mix(h + 3) % (sizeof(kTemplates) / sizeof(kTemplates[0]))];
  const std::string& rival_name = race.candidates[rival].name_zh;

  MockArticle m;
  m.candidate_id = cand.id;
  m.intended = t.sentiment;
  m.article.title = "【模擬】" + Fill(t.title, cand.name_zh, race.TitleZh(), rival_name);
  m.article.summary = Fill(t.body, cand.name_zh, race.TitleZh(), rival_name) +
                      "（本則為系統測試用模擬新聞，非真實報導。）";
  m.article.source = "MOCK";
  char url[96];
  std::snprintf(url, sizeof url, "mock://news/%016llx", static_cast<unsigned long long>(h));
  m.article.url = url;
  // Election day: 16:00 + minute (minutes may be negative: earlier that day).
  const int total = static_cast<int>(std::floor(16 * 60 + minute));
  char when[40];
  std::snprintf(when, sizeof when, "2026-11-28T%02d:%02d:00+08:00",
                std::max(0, std::min(23, total / 60)), ((total % 60) + 60) % 60);
  m.article.published_at = when;
  m.article.simulated = true;
  return m;
}

std::vector<MockArticle> MockNewsGenerator::Between(double from, double to, double per_hour) {
  std::vector<MockArticle> out;
  // One potential item slot per simulated minute; deterministic per minute.
  for (int minute = static_cast<int>(std::ceil(from)); minute < to; ++minute) {
    const uint64_t h = Mix(seed_ ^ (static_cast<uint64_t>(minute + 100000) * 0x51ED27));
    if (Unit(h) < per_hour / 60.0) out.push_back(Make(h, minute));
  }
  return out;
}

std::vector<MockArticle> MockNewsGenerator::Batch(int n) {
  std::vector<MockArticle> out;
  for (int i = 0; i < n; ++i) out.push_back(Make(Mix(seed_ + 7919ull * (i + 1)), -120));
  return out;
}

}  // namespace twn::news
