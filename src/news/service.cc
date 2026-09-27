#include "src/news/service.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"
#include "src/news/feed_parser.h"
#include "src/news/http.h"

namespace twn::news {

using nlohmann::json;

namespace {

std::string UrlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

}  // namespace

bool LoadNewsConfig(const std::string& path, const election::ElectionData& data,
                    NewsConfig* config, std::string* error) {
  std::string text;
  if (!election::ReadFile(path, &text)) {
    *error = "cannot read " + path;
    return false;
  }
  const json j = json::parse(text, nullptr, false);
  if (j.is_discarded()) {
    *error = path + ": invalid JSON";
    return false;
  }
  config->interval_s = j.value("interval_seconds", config->interval_s);
  config->classify_per_cycle = j.value("classify_per_cycle", config->classify_per_cycle);
  if (config->inbox_dir.empty()) config->inbox_dir = j.value("inbox_dir", "");
  for (const json& f : j.value("feeds", json::array())) {
    if (!f.value("enabled", true)) continue;
    config->feeds.push_back({f.value("name", ""), f.value("url", "")});
  }
  if (j.value("google_news_per_candidate", false)) {
    for (const election::Race& r : data.races()) {
      for (const election::Candidate& c : r.candidates) {
        const std::string q = "\"" + c.name_zh + "\" " + r.county_zh;
        config->feeds.push_back(
            {"Google News: " + c.name_zh,
             "https://news.google.com/rss/search?q=" + UrlEncode(q) + "&hl=zh-TW&gl=TW&ceid=TW:zh-Hant"});
      }
    }
  }
  if (j.contains("llm")) {
    // The file only fills values still at their defaults, so command-line
    // flags (and environment variables, applied later) take precedence.
    const json& l = j["llm"];
    const LlmConfig defaults;
    if (config->llm.base_url == defaults.base_url) {
      config->llm.base_url = l.value("base_url", config->llm.base_url);
    }
    if (config->llm.model == defaults.model) config->llm.model = l.value("model", config->llm.model);
    config->llm.json_mode = l.value("json_mode", config->llm.json_mode);
  }
  return true;
}

store::ClassifiedArticle ArticleFromEvent(const std::string& title, const std::string& url,
                                          const std::string& time,
                                          const std::vector<store::Assessment>& assessments,
                                          bool simulated) {
  store::ClassifiedArticle c;
  c.article.url = url;
  c.article.source = simulated ? "SIMULATION" : "Election feed";
  c.article.title = title;
  c.article.published_at = time;
  c.article.simulated = simulated;
  c.digest = title;
  c.model = "event-rule";
  c.assessments = assessments;
  return c;
}

NewsService::NewsService(NewsConfig config, std::vector<CandidateRef> candidates)
    : config_(std::move(config)), candidates_(std::move(candidates)) {
  ApplyLlmEnvironment(&config_.llm);
  const bool local = config_.llm.base_url.find("localhost") != std::string::npos ||
                     config_.llm.base_url.find("127.0.0.1") != std::string::npos;
  if (config_.use_llm && (!config_.llm.api_key.empty() || local)) {
    llm_ = std::make_unique<LlmClassifier>(config_.llm);
  }
  status_ = llm_ ? "LLM " + config_.llm.model : "heuristic";
}

NewsService::~NewsService() { Stop(); }

void NewsService::Start() {
  if (thread_.joinable()) return;
  stop_ = false;
  thread_ = std::thread([this] { Loop(); });
}

void NewsService::Stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void NewsService::Submit(store::ClassifiedArticle item) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    submitted_.push_back(std::move(item));
  }
  cv_.notify_all();
}

void NewsService::SubmitArticle(store::Article article) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    raw_.push_back(std::move(article));
  }
  cv_.notify_all();
}

std::string NewsService::Status() const {
  std::lock_guard<std::mutex> lock(mu_);
  return status_;
}

void NewsService::DrainSubmitted(store::Database* db) {
  std::deque<store::ClassifiedArticle> items;
  std::deque<store::Article> raw;
  {
    std::lock_guard<std::mutex> lock(mu_);
    items.swap(submitted_);
    raw.swap(raw_);
  }
  for (const store::Article& a : raw) db->UpsertArticle(a);
  for (const store::ClassifiedArticle& item : items) {
    bool inserted = false;
    const int64_t id = db->UpsertArticle(item.article, &inserted);
    if (id && inserted) db->SaveClassification(id, item.digest, item.model, item.assessments);
  }
}

void NewsService::Fetch(store::Database* db) {
  int added = 0, failed = 0;
  for (const FeedSource& f : config_.feeds) {
    if (stop_) return;
    const HttpResponse r = HttpGet(f.url);
    if (!r.ok()) {
      ++failed;
      continue;
    }
    for (const store::Article& a : ParseFeed(r.body, f.name)) {
      bool inserted = false;
      db->UpsertArticle(a, &inserted);
      added += inserted;
    }
    DrainSubmitted(db);  // keep event news flowing during long fetches
  }
  if (!config_.inbox_dir.empty() && std::filesystem::is_directory(config_.inbox_dir)) {
    for (const auto& entry : std::filesystem::directory_iterator(config_.inbox_dir)) {
      if (entry.path().extension() != ".jsonl") continue;
      std::string text;
      if (!election::ReadFile(entry.path().string(), &text)) continue;
      for (const store::Article& a : ParseJsonLines(text, nullptr)) {
        bool inserted = false;
        db->UpsertArticle(a, &inserted);
        added += inserted;
      }
      std::error_code ec;
      std::filesystem::rename(entry.path(), entry.path().string() + ".done", ec);
    }
  }
  std::lock_guard<std::mutex> lock(mu_);
  status_ = (llm_ ? "LLM " + config_.llm.model : std::string("heuristic")) + " · +" +
            std::to_string(added) + " articles" +
            (failed ? " · " + std::to_string(failed) + " feeds failed" : "");
}

void NewsService::Classify(store::Database* db) {
  const auto pending = db->UnclassifiedArticles(config_.classify_per_cycle);
  int done = 0;
  for (const store::Article& a : pending) {
    if (stop_) return;
    const std::vector<CandidateRef> mentioned = MentionedCandidates(a, candidates_);
    ClassifyResult result;
    std::string error;
    bool ok = false;
    if (mentioned.empty()) {
      // Not about any candidate: store as classified with no assessments.
      result.model = "no-candidate";
      result.digest = a.title;
      ok = true;
    } else if (llm_) {
      ok = llm_->Classify(a, mentioned, &result, &error);
      if (!ok) {
        std::lock_guard<std::mutex> lock(mu_);
        status_ = "LLM error (" + error.substr(0, 80) + "), using heuristic";
      }
    }
    if (!ok) ok = heuristic_.Classify(a, mentioned, &result, &error);
    if (ok) db->SaveClassification(a.id, result.digest, result.model, result.assessments);
    ++done;
    DrainSubmitted(db);
  }
  if (done) {
    std::lock_guard<std::mutex> lock(mu_);
    if (status_.find("error") == std::string::npos) {
      status_ = (llm_ ? "LLM " + config_.llm.model : std::string("heuristic")) + " · " +
                std::to_string(done) + " classified";
    }
  }
}

void NewsService::RunOnce(store::Database* db) {
  DrainSubmitted(db);
  Fetch(db);
  Classify(db);
  DrainSubmitted(db);
}

void NewsService::Loop() {
  store::Database db;
  std::string error;
  if (!db.Open(config_.db_path, &error)) {
    std::lock_guard<std::mutex> lock(mu_);
    status_ = "news DB error: " + error;
    return;
  }
  auto next_fetch = std::chrono::steady_clock::now();
  while (!stop_) {
    if (std::chrono::steady_clock::now() >= next_fetch) {
      Fetch(&db);
      Classify(&db);
      next_fetch = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(static_cast<int64_t>(config_.interval_s * 1000));
    }
    DrainSubmitted(&db);
    Classify(&db);  // new raw/mock articles are classified as they arrive
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait_for(lock, std::chrono::milliseconds(500),
                 [this] { return stop_.load() || !submitted_.empty() || !raw_.empty(); });
  }
  DrainSubmitted(&db);
}

}  // namespace twn::news
