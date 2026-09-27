// SQLite backend: app settings (e.g. the pinned home region), the results
// history and election-night event log, and the news pipeline (articles and
// per-candidate good/bad/neutral assessments).
//
// Each thread should use its own Database (SQLite connections are not shared
// across threads here); the file is opened in WAL mode so the news worker can
// write while the UI reads.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace twn::store {

enum class Sentiment { kBad = -1, kNeutral = 0, kGood = 1 };
const char* SentimentName(Sentiment s);  // "bad" / "neutral" / "good"
bool ParseSentiment(const std::string& s, Sentiment* out);

struct Article {
  int64_t id = 0;
  std::string url;  // unique key
  std::string source;
  std::string title;
  std::string summary;       // feed description / lead paragraph
  std::string published_at;  // as given by the feed
  std::string fetched_at;    // ISO-8601, set on insert when empty
  bool simulated = false;
};

struct Assessment {
  std::string candidate_id;
  Sentiment sentiment = Sentiment::kNeutral;
  std::string reason;
};

struct ClassifiedArticle {
  Article article;
  std::string digest;  // one-line summary produced by the classifier
  std::string model;   // "gpt-4o-mini", "heuristic", "event-rule", ...
  std::vector<Assessment> assessments;
};

struct SentimentCount {
  int good = 0;
  int bad = 0;
  int neutral = 0;
  int total() const { return good + bad + neutral; }
};

class Database {
 public:
  Database() = default;
  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // Opens (creating if needed) the database and applies the schema.
  // ":memory:" gives a private in-memory database (tests).
  bool Open(const std::string& path, std::string* error);
  void Close();
  bool is_open() const { return db_ != nullptr; }
  const std::string& last_error() const { return error_; }

  // Settings (key/value).
  std::optional<std::string> GetSetting(const std::string& key);
  bool SetSetting(const std::string& key, const std::string& value);
  bool DeleteSetting(const std::string& key);

  // Results history: one row per race and candidate for a snapshot time.
  struct RaceTotal {
    std::string race_id;
    std::string candidate_id;
    int64_t votes = 0;
    int units_counted = 0;
    int units_total = 0;
  };
  bool RecordTotals(const std::string& updated_at, bool simulated,
                    const std::vector<RaceTotal>& totals);
  bool RecordEvent(const std::string& time, const std::string& type, const std::string& race_id,
                   const std::string& leader_id, const std::string& other_id, int64_t margin,
                   double progress, bool simulated);
  int CountEvents(bool simulated);

  // News. Returns the article id; `*inserted` is false for a known URL.
  int64_t UpsertArticle(const Article& a, bool* inserted = nullptr);
  std::vector<Article> UnclassifiedArticles(int limit);
  bool SaveClassification(int64_t article_id, const std::string& digest, const std::string& model,
                          const std::vector<Assessment>& assessments);
  std::unordered_map<std::string, SentimentCount> CountsByCandidate();
  SentimentCount TotalCounts();
  std::vector<ClassifiedArticle> LatestNews(int limit);
  // Removes simulated articles/events (e.g. when a simulation restarts).
  bool ClearSimulated();

 private:
  bool Exec(const char* sql);

  sqlite3* db_ = nullptr;
  std::string error_;
};

std::string NowIso8601();

}  // namespace twn::store
