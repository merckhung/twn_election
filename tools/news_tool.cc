// news_tool: command-line access to the news pipeline and its SQLite store.
//
//   news_tool --fetch                 fetch data/news/feeds.json once and classify
//   news_tool --ingest=FILE.jsonl     add articles (one JSON object per line)
//   news_tool --classify              classify pending articles
//   news_tool --stats                 good/bad/neutral per candidate
//   news_tool --latest=N              latest classified articles
//   news_tool --mock=N --out=F.jsonl  write N labelled mock articles (or --rss=F.xml)
//   news_tool --eval=N                classify N mock articles, report accuracy
// Common: --db=FILE (default <root>/twn_election.db), --root=DIR,
//         --llm_base_url=URL --llm_model=NAME --no_llm  (key: $OPENAI_API_KEY)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <map>
#include <string>

#include "nlohmann/json.hpp"
#include "src/election/model.h"
#include "src/news/classifier.h"
#include "src/news/feed_parser.h"
#include "src/news/mock_news.h"
#include "src/news/service.h"
#include "src/store/db.h"

using namespace twn;

namespace {

std::string XmlEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else out += c;
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::string root = ".";
  if (const char* ws = std::getenv("BUILD_WORKSPACE_DIRECTORY")) root = ws;
  std::string db_path, ingest, out, rss;
  bool fetch = false, classify = false, stats = false, no_llm = false;
  int latest = 0, mock = 0, eval = 0;
  news::LlmConfig llm;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto val = [&](const char* flag) { return a.substr(std::strlen(flag)); };
    if (a.rfind("--root=", 0) == 0) root = val("--root=");
    else if (a.rfind("--db=", 0) == 0) db_path = val("--db=");
    else if (a == "--fetch") fetch = true;
    else if (a.rfind("--ingest=", 0) == 0) ingest = val("--ingest=");
    else if (a == "--classify") classify = true;
    else if (a == "--stats") stats = true;
    else if (a.rfind("--latest=", 0) == 0) latest = std::atoi(val("--latest=").c_str());
    else if (a.rfind("--mock=", 0) == 0) mock = std::atoi(val("--mock=").c_str());
    else if (a.rfind("--out=", 0) == 0) out = val("--out=");
    else if (a.rfind("--rss=", 0) == 0) rss = val("--rss=");
    else if (a.rfind("--eval=", 0) == 0) eval = std::atoi(val("--eval=").c_str());
    else if (a.rfind("--llm_base_url=", 0) == 0) llm.base_url = val("--llm_base_url=");
    else if (a.rfind("--llm_model=", 0) == 0) llm.model = val("--llm_model=");
    else if (a == "--no_llm") no_llm = true;
    else {
      std::fprintf(stderr, "unknown flag %s (see the header of tools/news_tool.cc)\n", a.c_str());
      return 2;
    }
  }
  election::ElectionData data;
  std::string error;
  if (!data.Load(root + "/data/election/2026", &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  const std::vector<news::CandidateRef> refs = news::CandidateRefs(data);

  if (mock > 0) {
    news::MockNewsGenerator gen(&data, 2026);
    const auto items = gen.Batch(mock);
    if (!rss.empty()) {
      std::ofstream f(rss);
      f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rss version=\"2.0\"><channel>"
           "<title>twn_election mock news</title>\n";
      for (const auto& m : items) {
        f << "<item><title>" << XmlEscape(m.article.title) << "</title><link>" << m.article.url
          << "</link><pubDate>" << m.article.published_at << "</pubDate><description><![CDATA["
          << m.article.summary << "]]></description></item>\n";
      }
      f << "</channel></rss>\n";
      std::printf("wrote %zu mock items to %s\n", items.size(), rss.c_str());
    } else {
      std::ofstream file;
      if (!out.empty()) file.open(out);
      std::ostream& o = out.empty() ? std::cout : file;
      for (const auto& m : items) {
        o << nlohmann::json{{"title", m.article.title}, {"url", m.article.url},
                            {"source", m.article.source}, {"published", m.article.published_at},
                            {"summary", m.article.summary},
                            {"intended", {{"candidate_id", m.candidate_id},
                                          {"sentiment", store::SentimentName(m.intended)}}}}
                 .dump()
          << "\n";
      }
    }
    return 0;
  }

  news::ApplyLlmEnvironment(&llm);
  std::unique_ptr<news::Classifier> classifier;
  if (!no_llm && (!llm.api_key.empty() || llm.base_url.find("127.0.0.1") != std::string::npos ||
                  llm.base_url.find("localhost") != std::string::npos)) {
    classifier = std::make_unique<news::LlmClassifier>(llm);
  } else {
    classifier = std::make_unique<news::HeuristicClassifier>();
  }

  if (eval > 0) {
    news::MockNewsGenerator gen(&data, 777);
    int correct = 0, total = 0, errors = 0;
    std::map<std::string, std::map<std::string, int>> confusion;
    for (const auto& m : gen.Batch(eval)) {
      news::ClassifyResult r;
      const auto mentioned = news::MentionedCandidates(m.article, refs);
      if (!classifier->Classify(m.article, mentioned, &r, &error)) {
        ++errors;
        continue;
      }
      std::string got = "missing";
      for (const auto& a : r.assessments) {
        if (a.candidate_id == m.candidate_id) got = store::SentimentName(a.sentiment);
      }
      const std::string want = store::SentimentName(m.intended);
      confusion[want][got]++;
      correct += got == want;
      ++total;
    }
    std::printf("classifier: %s\naccuracy: %d/%d (%.1f%%), errors: %d\n", classifier->Name().c_str(),
                correct, total, total ? 100.0 * correct / total : 0.0, errors);
    for (const auto& [want, row] : confusion) {
      std::printf("  intended %-8s ->", want.c_str());
      for (const auto& [got, n] : row) std::printf("  %s:%d", got.c_str(), n);
      std::printf("\n");
    }
    return 0;
  }

  store::Database db;
  if (db_path.empty()) db_path = root + "/twn_election.db";
  if (!db.Open(db_path, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  if (!ingest.empty()) {
    std::string text;
    if (!election::ReadFile(ingest, &text)) {
      std::fprintf(stderr, "cannot read %s\n", ingest.c_str());
      return 1;
    }
    int added = 0;
    for (const auto& a : news::ParseJsonLines(text, &error)) {
      bool inserted = false;
      db.UpsertArticle(a, &inserted);
      added += inserted;
    }
    std::printf("ingested %d new articles\n", added);
  }
  if (fetch || classify) {
    news::NewsConfig config;
    config.db_path = db_path;
    config.llm = llm;
    config.use_llm = !no_llm;
    if (fetch && !news::LoadNewsConfig(root + "/data/news/feeds.json", data, &config, &error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
    }
    config.classify_per_cycle = 1000;
    news::NewsService service(config, refs);
    service.RunOnce(&db);
    std::printf("%s\n", service.Status().c_str());
  }
  if (stats) {
    const auto counts = db.CountsByCandidate();
    const store::SentimentCount t = db.TotalCounts();
    std::printf("total: good %d, bad %d, neutral %d\n", t.good, t.bad, t.neutral);
    for (const auto& race : data.races()) {
      for (const auto& c : race.candidates) {
        auto it = counts.find(c.id);
        if (it == counts.end()) continue;
        std::printf("%s %-8s %-10s good %3d  bad %3d  neutral %3d  net %+d\n", c.id.c_str(),
                    c.name_zh.c_str(), race.TitleZh().c_str(), it->second.good, it->second.bad,
                    it->second.neutral, it->second.good - it->second.bad);
      }
    }
  }
  if (latest > 0) {
    for (const auto& n : db.LatestNews(latest)) {
      std::printf("[%s] %s (%s)\n", n.model.c_str(), n.article.title.c_str(), n.article.source.c_str());
      for (const auto& a : n.assessments) {
        std::printf("    %s %s %s\n", a.candidate_id.c_str(), store::SentimentName(a.sentiment),
                    a.reason.c_str());
      }
    }
  }
  return 0;
}
