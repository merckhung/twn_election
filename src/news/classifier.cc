#include "src/news/classifier.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "nlohmann/json.hpp"
#include "src/news/http.h"

namespace twn::news {

using nlohmann::json;

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// English name variants: "Puma Shen (Shen Po-yang)" -> {"puma shen", "shen po-yang"}.
std::vector<std::string> EnglishNames(const std::string& name_en) {
  std::vector<std::string> out;
  const size_t paren = name_en.find('(');
  std::string main = name_en.substr(0, paren);
  while (!main.empty() && main.back() == ' ') main.pop_back();
  if (!main.empty()) out.push_back(Lower(main));
  if (paren != std::string::npos) {
    const size_t close = name_en.find(')', paren);
    out.push_back(Lower(name_en.substr(paren + 1, close - paren - 1)));
  }
  return out;
}

std::string ArticleText(const store::Article& a) { return a.title + "\n" + a.summary; }

// Sentence splitter for mixed Chinese/English text.
std::vector<std::string> Sentences(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = text[i];
    const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
    const std::string ch = text.substr(i, n);
    cur += ch;
    i += n;
    if (ch == "。" || ch == "！" || ch == "？" || ch == "；" || ch == "\n" || ch == "!" ||
        ch == "?" || (ch == "." && (i >= text.size() || text[i] == ' '))) {
      out.push_back(cur);
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

const std::vector<std::string>& PositiveWords() {
  static const auto* w = new std::vector<std::string>{
      "領先", "勝選", "當選", "勝出", "獲勝", "大勝", "看好", "支持度上升", "上升", "熱烈", "力挺", "背書", "站台", "聲勢",
      "民調領先", "拿下", "宣布勝選", "感謝", "肯定", "好評", "政績", "獲得", "突破", "反超", "超越",
      "逆轉勝", "穩健", "開出紅盤", "凍蒜", "勝利", "lead", "leads", "leading", "wins", "won",
      "victory", "endorse", "endorsed", "endorsement", "surge", "praised", "ahead", "support"};
  return *w;
}

const std::vector<std::string>& NegativeWords() {
  static const auto* w = new std::vector<std::string>{
      "落後", "敗選", "落選", "爭議", "弊案", "起訴", "醜聞", "批評", "抨擊", "失言", "調查", "賄選",
      "退選", "質疑", "違法", "判刑", "搜索", "約談", "承認敗選", "被超越", "失利", "重挫", "炎上",
      "告發", "不滿", "緋聞", "學歷", "抄襲", "下滑", "吃緊", "不如預期", "跳票", "遭批", "滅火", "trails", "trailing", "behind", "scandal",
      "indicted", "criticized", "criticised", "controversy", "loses", "lost", "concedes",
      "conceded", "investigation", "probe", "accused", "defeat"};
  return *w;
}

// "A 反超 B" style phrases: good for the name before, bad for the one after.
const std::vector<std::string>& OvertakeWords() {
  static const auto* w = new std::vector<std::string>{"領先", "反超", "超越", "擊敗", "打敗",
                                                      "勝過", "leads", "overtakes", "beats",
                                                      "defeats"};
  return *w;
}

int Count(const std::string& s, const std::vector<std::string>& words) {
  const std::string lower = Lower(s);
  int n = 0;
  for (const std::string& w : words) {
    for (size_t p = lower.find(w); p != std::string::npos; p = lower.find(w, p + w.size())) ++n;
  }
  return n;
}

size_t FindName(const std::string& sentence, const CandidateRef& c) {
  size_t p = sentence.find(c.name_zh);
  if (p != std::string::npos) return p;
  const std::string lower = Lower(sentence);
  for (const std::string& en : EnglishNames(c.name_en)) {
    if (en.size() >= 5 && (p = lower.find(en)) != std::string::npos) return p;
  }
  return std::string::npos;
}

}  // namespace

std::vector<CandidateRef> CandidateRefs(const election::ElectionData& data) {
  std::vector<CandidateRef> out;
  for (const election::Race& r : data.races()) {
    for (const election::Candidate& c : r.candidates) {
      out.push_back({c.id, c.name_zh, c.name_en, data.party(c.party).short_zh, r.TitleZh()});
    }
  }
  return out;
}

std::vector<CandidateRef> MentionedCandidates(const store::Article& a,
                                              const std::vector<CandidateRef>& all) {
  const std::string text = ArticleText(a);
  std::vector<CandidateRef> out;
  for (const CandidateRef& c : all) {
    // Two-character names are common words too often; require the full name.
    if (FindName(text, c) != std::string::npos) out.push_back(c);
  }
  return out;
}

bool HeuristicClassifier::Classify(const store::Article& a,
                                   const std::vector<CandidateRef>& candidates,
                                   ClassifyResult* out, std::string*) {
  out->model = Name();
  out->digest = a.title;
  out->assessments.clear();
  const std::vector<std::string> sentences = Sentences(ArticleText(a));
  for (const CandidateRef& c : candidates) {
    int score = 0;
    for (const std::string& s : sentences) {
      const size_t at = FindName(s, c);
      if (at == std::string::npos) continue;
      // Comparative phrase with another candidate in the same sentence.
      bool comparative = false;
      for (const CandidateRef& other : candidates) {
        if (other.id == c.id) continue;
        const size_t ot = FindName(s, other);
        if (ot == std::string::npos) continue;
        const std::string lower = Lower(s);
        const size_t first = std::min(at, ot), second = std::max(at, ot);
        auto between = [&](const std::string& w) {
          const size_t wp = lower.find(w, first);
          return wp != std::string::npos && wp < second;
        };
        const bool ahead = std::any_of(OvertakeWords().begin(), OvertakeWords().end(), between);
        const bool behind = between("落後") || between("trails") || between("behind");
        if (ahead || behind) {
          // "A 領先 B": good for A (named first), bad for B; "A 落後 B" the reverse.
          const bool c_first = at < ot;
          score += (ahead != c_first) ? -2 : 2;
          comparative = true;
        }
      }
      if (!comparative) score += Count(s, PositiveWords()) - Count(s, NegativeWords());
    }
    store::Assessment as;
    as.candidate_id = c.id;
    as.sentiment = score > 0 ? store::Sentiment::kGood
                   : score < 0 ? store::Sentiment::kBad
                               : store::Sentiment::kNeutral;
    as.reason = "keyword score " + std::to_string(score);
    out->assessments.push_back(std::move(as));
  }
  return true;
}

void ApplyLlmEnvironment(LlmConfig* config) {
  auto env = [](const char* name) -> std::string {
    const char* v = std::getenv(name);
    return v ? v : "";
  };
  if (config->api_key.empty()) config->api_key = env("TWN_LLM_API_KEY");
  if (config->api_key.empty()) config->api_key = env("OPENAI_API_KEY");
  // Environment only replaces defaults: explicit flags win.
  const LlmConfig defaults;
  if (config->base_url == defaults.base_url) {
    if (!env("TWN_LLM_BASE_URL").empty()) config->base_url = env("TWN_LLM_BASE_URL");
    else if (!env("OPENAI_BASE_URL").empty()) config->base_url = env("OPENAI_BASE_URL");
  }
  if (config->model == defaults.model && !env("TWN_LLM_MODEL").empty()) {
    config->model = env("TWN_LLM_MODEL");
  }
}

std::string LlmClassifier::BuildRequest(const LlmConfig& config, const store::Article& a,
                                        const std::vector<CandidateRef>& candidates) {
  std::string list;
  for (const CandidateRef& c : candidates) {
    list += "- " + c.id + " | " + c.name_zh + " | " + c.name_en + " | " + c.party + " | " +
            c.race_title + "\n";
  }
  const std::string system =
      "You analyse Taiwanese news for the 2026-11-28 local elections (九合一選舉). For each "
      "listed candidate the article is about, decide whether the news is good, bad or "
      "neutral for that candidate's electoral prospects. Judge the facts reported, not the "
      "outlet's tone; routine campaign activity is neutral. Use only candidate ids from the "
      "list. Reply with a single JSON object and nothing else:\n"
      "{\"summary\": \"<one sentence in Traditional Chinese, <= 60 characters>\", "
      "\"assessments\": [{\"candidate_id\": \"<id>\", \"sentiment\": \"good|bad|neutral\", "
      "\"reason\": \"<Traditional Chinese, <= 40 characters>\"}]}";
  const std::string user = "Candidates (id | 姓名 | name | party | race):\n" + list +
                           "\nArticle source: " + a.source + "\nPublished: " + a.published_at +
                           "\nTitle: " + a.title + "\nText: " + a.summary;
  json req = {{"model", config.model},
              {"temperature", config.temperature},
              {"messages",
               json::array({{{"role", "system"}, {"content", system}},
                            {{"role", "user"}, {"content", user}}})}};
  if (config.json_mode) req["response_format"] = {{"type", "json_object"}};
  return req.dump();
}

bool LlmClassifier::ParseResponse(const std::string& body,
                                  const std::vector<CandidateRef>& candidates,
                                  ClassifyResult* out, std::string* error) {
  const json resp = json::parse(body, nullptr, false);
  if (resp.is_discarded()) {
    *error = "LLM response is not JSON";
    return false;
  }
  if (resp.contains("error")) {
    *error = "LLM error: " + resp["error"].dump();
    return false;
  }
  std::string content;
  try {
    content = resp.at("choices").at(0).at("message").at("content").get<std::string>();
  } catch (const json::exception&) {
    *error = "unexpected LLM response shape";
    return false;
  }
  // Tolerate ```json fences and prose around the object.
  const size_t b = content.find('{'), e = content.rfind('}');
  if (b == std::string::npos || e == std::string::npos || e < b) {
    *error = "no JSON object in LLM answer";
    return false;
  }
  const json answer = json::parse(content.substr(b, e - b + 1), nullptr, false);
  if (answer.is_discarded() || !answer.is_object()) {
    *error = "LLM answer is not valid JSON";
    return false;
  }
  if (resp.contains("model") && resp["model"].is_string()) {
    out->model = resp["model"].get<std::string>();
  }
  out->digest = answer.value("summary", "");
  out->assessments.clear();
  for (const json& item : answer.value("assessments", json::array())) {
    const std::string id = item.value("candidate_id", "");
    const bool known = std::any_of(candidates.begin(), candidates.end(),
                                   [&](const CandidateRef& c) { return c.id == id; });
    store::Sentiment s;
    if (!known || !store::ParseSentiment(Lower(item.value("sentiment", "")), &s)) continue;
    out->assessments.push_back({id, s, item.value("reason", "")});
  }
  return true;
}

bool LlmClassifier::Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                             ClassifyResult* out, std::string* error) {
  out->model = config_.model;
  std::string url = config_.base_url;
  while (!url.empty() && url.back() == '/') url.pop_back();
  url += "/chat/completions";
  std::vector<std::string> headers;
  if (!config_.api_key.empty()) headers.push_back("Authorization: Bearer " + config_.api_key);
  const HttpResponse r =
      HttpPostJson(url, BuildRequest(config_, a, candidates), headers, config_.timeout_s);
  if (!r.error.empty()) {
    *error = "LLM request failed: " + r.error;
    return false;
  }
  if (r.status != 200) {
    *error = "LLM HTTP " + std::to_string(r.status) + ": " + r.body.substr(0, 300);
    return false;
  }
  return ParseResponse(r.body, candidates, out, error);
}

}  // namespace twn::news
