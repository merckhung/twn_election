// Background news pipeline: periodically pulls RSS/Atom feeds and JSONL
// drop files, stores new articles in SQLite, classifies them (LLM when an
// OpenAI-compatible endpoint is configured, keyword heuristic otherwise) and
// records per-candidate good/bad/neutral assessments.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "src/news/classifier.h"
#include "src/store/db.h"

namespace twn::news {

struct FeedSource {
  std::string name;
  std::string url;  // http(s):// or file://
};

struct NewsConfig {
  std::string db_path;
  std::vector<FeedSource> feeds;
  std::string inbox_dir;  // *.jsonl files are ingested (and renamed *.done)
  double interval_s = 600;
  int classify_per_cycle = 40;
  bool use_llm = true;  // falls back to the heuristic when no endpoint/key
  LlmConfig llm;
};

// Reads data/news/feeds.json. Expands {"google_news_per_candidate": true}
// into one Google News search feed per candidate.
bool LoadNewsConfig(const std::string& path, const election::ElectionData& data,
                    NewsConfig* config, std::string* error);

// Classification rules for news derived from election-night events
// (simulation or feed): lead change = good for the new leader / bad for the
// overtaken, victory declared = good, concession = bad, ...
store::ClassifiedArticle ArticleFromEvent(const std::string& title, const std::string& url,
                                          const std::string& time,
                                          const std::vector<store::Assessment>& assessments,
                                          bool simulated);

class NewsService {
 public:
  NewsService(NewsConfig config, std::vector<CandidateRef> candidates);
  ~NewsService();

  void Start();
  void Stop();
  // Pre-classified item (e.g. generated from an election-night event).
  void Submit(store::ClassifiedArticle item);
  // Raw article to be stored and classified by the worker (mock news).
  void SubmitArticle(store::Article article);
  // Runs one fetch + classify cycle synchronously (CLI / tests).
  void RunOnce(store::Database* db);

  std::string Status() const;

 private:
  void Loop();
  void Fetch(store::Database* db);
  void Classify(store::Database* db);
  void DrainSubmitted(store::Database* db);

  NewsConfig config_;
  std::vector<CandidateRef> candidates_;
  std::unique_ptr<Classifier> llm_;
  HeuristicClassifier heuristic_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<store::ClassifiedArticle> submitted_;
  std::deque<store::Article> raw_;
  std::string status_;
};

}  // namespace twn::news
