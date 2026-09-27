#include "src/store/db.h"

#include <chrono>
#include <cstdio>
#include <ctime>

#include "sqlite3.h"

namespace twn::store {
namespace {

constexpr const char* kSchema = R"sql(
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS settings (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS race_totals (
  updated_at    TEXT NOT NULL,
  simulated     INTEGER NOT NULL,
  race_id       TEXT NOT NULL,
  candidate_id  TEXT NOT NULL,
  votes         INTEGER NOT NULL,
  units_counted INTEGER NOT NULL,
  units_total   INTEGER NOT NULL,
  PRIMARY KEY (updated_at, simulated, race_id, candidate_id)
);
CREATE TABLE IF NOT EXISTS events (
  id         INTEGER PRIMARY KEY,
  time       TEXT NOT NULL,
  type       TEXT NOT NULL,
  race_id    TEXT NOT NULL,
  leader_id  TEXT,
  other_id   TEXT,
  margin     INTEGER,
  progress   REAL,
  simulated  INTEGER NOT NULL,
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS articles (
  id           INTEGER PRIMARY KEY,
  url          TEXT NOT NULL UNIQUE,
  source       TEXT,
  title        TEXT NOT NULL,
  summary      TEXT,
  published_at TEXT,
  fetched_at   TEXT NOT NULL,
  simulated    INTEGER NOT NULL DEFAULT 0,
  classified   INTEGER NOT NULL DEFAULT 0,
  digest       TEXT,
  model        TEXT
);
CREATE TABLE IF NOT EXISTS assessments (
  article_id   INTEGER NOT NULL REFERENCES articles(id) ON DELETE CASCADE,
  candidate_id TEXT NOT NULL,
  sentiment    INTEGER NOT NULL,  -- -1 bad, 0 neutral, +1 good
  reason       TEXT,
  PRIMARY KEY (article_id, candidate_id)
);
CREATE INDEX IF NOT EXISTS assessments_by_candidate ON assessments(candidate_id);
CREATE INDEX IF NOT EXISTS articles_unclassified ON articles(classified, id);
)sql";

// RAII statement wrapper.
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr); }
  ~Stmt() { sqlite3_finalize(stmt_); }
  bool ok() const { return stmt_ != nullptr; }
  Stmt& Bind(int i, const std::string& v) {
    sqlite3_bind_text(stmt_, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
  }
  Stmt& Bind(int i, int64_t v) {
    sqlite3_bind_int64(stmt_, i, v);
    return *this;
  }
  Stmt& Bind(int i, double v) {
    sqlite3_bind_double(stmt_, i, v);
    return *this;
  }
  int Step() { return sqlite3_step(stmt_); }
  void Reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }
  std::string Text(int col) const {
    const unsigned char* t = sqlite3_column_text(stmt_, col);
    return t ? reinterpret_cast<const char*>(t) : "";
  }
  int64_t Int(int col) const { return sqlite3_column_int64(stmt_, col); }

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace

const char* SentimentName(Sentiment s) {
  switch (s) {
    case Sentiment::kGood: return "good";
    case Sentiment::kBad: return "bad";
    default: return "neutral";
  }
}

bool ParseSentiment(const std::string& s, Sentiment* out) {
  if (s == "good" || s == "positive" || s == "+1" || s == "1") *out = Sentiment::kGood;
  else if (s == "bad" || s == "negative" || s == "-1") *out = Sentiment::kBad;
  else if (s == "neutral" || s == "0" || s == "mixed") *out = Sentiment::kNeutral;
  else return false;
  return true;
}

std::string NowIso8601() {
  const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

Database::~Database() { Close(); }

void Database::Close() {
  if (db_) sqlite3_close(db_);
  db_ = nullptr;
}

bool Database::Exec(const char* sql) {
  char* msg = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
    error_ = msg ? msg : "sqlite error";
    sqlite3_free(msg);
    return false;
  }
  return true;
}

bool Database::Open(const std::string& path, std::string* error) {
  Close();
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    *error = "cannot open " + path + ": " + (db_ ? sqlite3_errmsg(db_) : "?");
    Close();
    return false;
  }
  sqlite3_busy_timeout(db_, 3000);
  if (!Exec(kSchema)) {
    *error = "schema: " + error_;
    Close();
    return false;
  }
  return true;
}

std::optional<std::string> Database::GetSetting(const std::string& key) {
  Stmt s(db_, "SELECT value FROM settings WHERE key = ?1");
  s.Bind(1, key);
  if (s.Step() == SQLITE_ROW) return s.Text(0);
  return std::nullopt;
}

bool Database::SetSetting(const std::string& key, const std::string& value) {
  Stmt s(db_,
         "INSERT INTO settings(key, value) VALUES(?1, ?2) "
         "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
  s.Bind(1, key).Bind(2, value);
  return s.Step() == SQLITE_DONE;
}

bool Database::DeleteSetting(const std::string& key) {
  Stmt s(db_, "DELETE FROM settings WHERE key = ?1");
  s.Bind(1, key);
  return s.Step() == SQLITE_DONE;
}

bool Database::RecordTotals(const std::string& updated_at, bool simulated,
                            const std::vector<RaceTotal>& totals) {
  if (!Exec("BEGIN")) return false;
  Stmt s(db_,
         "INSERT OR REPLACE INTO race_totals(updated_at, simulated, race_id, candidate_id, "
         "votes, units_counted, units_total) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
  for (const RaceTotal& t : totals) {
    s.Bind(1, updated_at).Bind(2, int64_t{simulated}).Bind(3, t.race_id).Bind(4, t.candidate_id);
    s.Bind(5, t.votes).Bind(6, int64_t{t.units_counted}).Bind(7, int64_t{t.units_total});
    if (s.Step() != SQLITE_DONE) {
      Exec("ROLLBACK");
      return false;
    }
    s.Reset();
  }
  return Exec("COMMIT");
}

bool Database::RecordEvent(const std::string& time, const std::string& type,
                           const std::string& race_id, const std::string& leader_id,
                           const std::string& other_id, int64_t margin, double progress,
                           bool simulated) {
  Stmt s(db_,
         "INSERT INTO events(time, type, race_id, leader_id, other_id, margin, progress, "
         "simulated, created_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
  s.Bind(1, time).Bind(2, type).Bind(3, race_id).Bind(4, leader_id).Bind(5, other_id);
  s.Bind(6, margin).Bind(7, progress).Bind(8, int64_t{simulated}).Bind(9, NowIso8601());
  return s.Step() == SQLITE_DONE;
}

int Database::CountEvents(bool simulated) {
  Stmt s(db_, "SELECT COUNT(*) FROM events WHERE simulated = ?1");
  s.Bind(1, int64_t{simulated});
  return s.Step() == SQLITE_ROW ? static_cast<int>(s.Int(0)) : 0;
}

int64_t Database::UpsertArticle(const Article& a, bool* inserted) {
  Stmt ins(db_,
           "INSERT OR IGNORE INTO articles(url, source, title, summary, published_at, "
           "fetched_at, simulated) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
  ins.Bind(1, a.url).Bind(2, a.source).Bind(3, a.title).Bind(4, a.summary);
  ins.Bind(5, a.published_at).Bind(6, a.fetched_at.empty() ? NowIso8601() : a.fetched_at);
  ins.Bind(7, int64_t{a.simulated});
  if (ins.Step() != SQLITE_DONE) {
    error_ = sqlite3_errmsg(db_);
    return 0;
  }
  const bool fresh = sqlite3_changes(db_) > 0;
  if (inserted) *inserted = fresh;
  if (fresh) return sqlite3_last_insert_rowid(db_);
  Stmt q(db_, "SELECT id FROM articles WHERE url = ?1");
  q.Bind(1, a.url);
  return q.Step() == SQLITE_ROW ? q.Int(0) : 0;
}

std::vector<Article> Database::UnclassifiedArticles(int limit) {
  std::vector<Article> out;
  Stmt s(db_,
         "SELECT id, url, source, title, summary, published_at, fetched_at, simulated "
         "FROM articles WHERE classified = 0 ORDER BY id LIMIT ?1");
  s.Bind(1, int64_t{limit});
  while (s.Step() == SQLITE_ROW) {
    Article a;
    a.id = s.Int(0);
    a.url = s.Text(1);
    a.source = s.Text(2);
    a.title = s.Text(3);
    a.summary = s.Text(4);
    a.published_at = s.Text(5);
    a.fetched_at = s.Text(6);
    a.simulated = s.Int(7) != 0;
    out.push_back(std::move(a));
  }
  return out;
}

bool Database::SaveClassification(int64_t article_id, const std::string& digest,
                                  const std::string& model,
                                  const std::vector<Assessment>& assessments) {
  if (!Exec("BEGIN")) return false;
  Stmt del(db_, "DELETE FROM assessments WHERE article_id = ?1");
  del.Bind(1, article_id);
  del.Step();
  Stmt ins(db_,
           "INSERT OR REPLACE INTO assessments(article_id, candidate_id, sentiment, reason) "
           "VALUES(?1, ?2, ?3, ?4)");
  for (const Assessment& a : assessments) {
    ins.Bind(1, article_id).Bind(2, a.candidate_id);
    ins.Bind(3, static_cast<int64_t>(a.sentiment)).Bind(4, a.reason);
    if (ins.Step() != SQLITE_DONE) {
      error_ = sqlite3_errmsg(db_);
      Exec("ROLLBACK");
      return false;
    }
    ins.Reset();
  }
  Stmt upd(db_, "UPDATE articles SET classified = 1, digest = ?2, model = ?3 WHERE id = ?1");
  upd.Bind(1, article_id).Bind(2, digest).Bind(3, model);
  upd.Step();
  return Exec("COMMIT");
}

std::unordered_map<std::string, SentimentCount> Database::CountsByCandidate() {
  std::unordered_map<std::string, SentimentCount> out;
  Stmt s(db_,
         "SELECT candidate_id, SUM(sentiment = 1), SUM(sentiment = -1), SUM(sentiment = 0) "
         "FROM assessments GROUP BY candidate_id");
  while (s.Step() == SQLITE_ROW) {
    SentimentCount& c = out[s.Text(0)];
    c.good = static_cast<int>(s.Int(1));
    c.bad = static_cast<int>(s.Int(2));
    c.neutral = static_cast<int>(s.Int(3));
  }
  return out;
}

SentimentCount Database::TotalCounts() {
  SentimentCount c;
  Stmt s(db_,
         "SELECT SUM(sentiment = 1), SUM(sentiment = -1), SUM(sentiment = 0) FROM assessments");
  if (s.Step() == SQLITE_ROW) {
    c.good = static_cast<int>(s.Int(0));
    c.bad = static_cast<int>(s.Int(1));
    c.neutral = static_cast<int>(s.Int(2));
  }
  return c;
}

std::vector<ClassifiedArticle> Database::LatestNews(int limit) {
  std::vector<ClassifiedArticle> out;
  Stmt s(db_,
         "SELECT id, url, source, title, summary, published_at, fetched_at, simulated, digest, "
         "model FROM articles WHERE classified = 1 ORDER BY id DESC LIMIT ?1");
  s.Bind(1, int64_t{limit});
  Stmt as(db_,
          "SELECT candidate_id, sentiment, reason FROM assessments WHERE article_id = ?1 "
          "ORDER BY sentiment DESC");
  while (s.Step() == SQLITE_ROW) {
    ClassifiedArticle c;
    c.article.id = s.Int(0);
    c.article.url = s.Text(1);
    c.article.source = s.Text(2);
    c.article.title = s.Text(3);
    c.article.summary = s.Text(4);
    c.article.published_at = s.Text(5);
    c.article.fetched_at = s.Text(6);
    c.article.simulated = s.Int(7) != 0;
    c.digest = s.Text(8);
    c.model = s.Text(9);
    as.Bind(1, c.article.id);
    while (as.Step() == SQLITE_ROW) {
      Assessment a;
      a.candidate_id = as.Text(0);
      a.sentiment = static_cast<Sentiment>(as.Int(1));
      a.reason = as.Text(2);
      c.assessments.push_back(std::move(a));
    }
    as.Reset();
    out.push_back(std::move(c));
  }
  return out;
}

bool Database::ClearSimulated() {
  return Exec(
      "DELETE FROM assessments WHERE article_id IN (SELECT id FROM articles WHERE simulated = 1);"
      "DELETE FROM articles WHERE simulated = 1;"
      "DELETE FROM events WHERE simulated = 1;"
      "DELETE FROM race_totals WHERE simulated = 1;");
}

}  // namespace twn::store
