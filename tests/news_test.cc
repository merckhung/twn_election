// News pipeline: SQLite store, feed parsing, classifiers, LLM protocol, mocks.
#include <map>
#include <set>

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"
#include "src/news/classifier.h"
#include "src/news/feed_parser.h"
#include "src/news/mock_news.h"
#include "src/news/service.h"
#include "src/store/db.h"

namespace twn {
namespace {

using store::Sentiment;

TEST(Store, SettingsPinnedHome) {
  store::Database db;
  std::string error;
  ASSERT_TRUE(db.Open(":memory:", &error)) << error;
  EXPECT_FALSE(db.GetSetting("home_region").has_value());
  ASSERT_TRUE(db.SetSetting("home_region", "63000"));
  EXPECT_EQ(db.GetSetting("home_region").value(), "63000");
  ASSERT_TRUE(db.SetSetting("home_region", "67000"));
  EXPECT_EQ(db.GetSetting("home_region").value(), "67000");
  ASSERT_TRUE(db.DeleteSetting("home_region"));
  EXPECT_FALSE(db.GetSetting("home_region").has_value());
}

TEST(Store, ArticlesAssessmentsAndCounts) {
  store::Database db;
  std::string error;
  ASSERT_TRUE(db.Open(":memory:", &error)) << error;
  store::Article a;
  a.url = "https://example.com/1";
  a.title = "A";
  bool inserted = false;
  const int64_t id = db.UpsertArticle(a, &inserted);
  EXPECT_TRUE(inserted);
  EXPECT_EQ(db.UpsertArticle(a, &inserted), id);  // same URL: deduplicated
  EXPECT_FALSE(inserted);
  ASSERT_EQ(db.UnclassifiedArticles(10).size(), 1u);
  ASSERT_TRUE(db.SaveClassification(id, "digest", "test",
                                    {{"63000-02", Sentiment::kGood, "r"},
                                     {"63000-05", Sentiment::kBad, "r"}}));
  EXPECT_TRUE(db.UnclassifiedArticles(10).empty());
  store::Article b;
  b.url = "sim://2";
  b.title = "B";
  b.simulated = true;
  const int64_t id2 = db.UpsertArticle(b);
  db.SaveClassification(id2, "", "test", {{"63000-02", Sentiment::kGood, ""}});
  auto counts = db.CountsByCandidate();
  EXPECT_EQ(counts["63000-02"].good, 2);
  EXPECT_EQ(counts["63000-05"].bad, 1);
  EXPECT_EQ(db.TotalCounts().total(), 3);
  auto latest = db.LatestNews(5);
  ASSERT_EQ(latest.size(), 2u);
  EXPECT_EQ(latest[0].article.title, "B");  // newest first
  EXPECT_EQ(latest[1].assessments.size(), 2u);
  ASSERT_TRUE(db.ClearSimulated());
  EXPECT_EQ(db.CountsByCandidate()["63000-02"].good, 1);
}

TEST(Store, EventsAndTotals) {
  store::Database db;
  std::string error;
  ASSERT_TRUE(db.Open(":memory:", &error)) << error;
  EXPECT_TRUE(db.RecordEvent("2026-11-28T19:00:00+08:00", "lead_change", "63000-mayor",
                             "63000-02", "63000-05", 123, 0.4, true));
  EXPECT_TRUE(db.RecordTotals("2026-11-28T19:00:00+08:00", true,
                              {{"63000-mayor", "63000-02", 100, 1, 2}}));
  EXPECT_EQ(db.CountEvents(true), 1);
  EXPECT_EQ(db.CountEvents(false), 0);
}

TEST(Feeds, ParsesRss) {
  const char* rss = R"(<?xml version="1.0"?><rss><channel><title>t</title>
    <item><title><![CDATA[蔣萬安 出席活動 &amp; 拜票]]></title>
      <link>https://news.example/a</link><pubDate>Sat, 28 Nov 2026 10:00:00 +0800</pubDate>
      <description>&lt;p&gt;市長 &lt;b&gt;今日&lt;/b&gt; 拜票&lt;/p&gt;</description>
      <source url="https://x">範例新聞</source></item>
    <item><title>No link</title></item>
  </channel></rss>)";
  auto items = news::ParseFeed(rss, "Feed");
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].title, "蔣萬安 出席活動 & 拜票");
  EXPECT_EQ(items[0].url, "https://news.example/a");
  EXPECT_EQ(items[0].summary, "市長 今日 拜票");
  EXPECT_EQ(items[0].source, "範例新聞");
}

TEST(Feeds, ParsesAtomAndJsonLines) {
  const char* atom = R"(<feed xmlns="http://www.w3.org/2005/Atom"><entry>
    <title>Title &#x81FA;</title><link rel="alternate" href="https://a/1"/>
    <updated>2026-11-28T10:00:00Z</updated><summary>S</summary></entry></feed>)";
  auto items = news::ParseFeed(atom, "Atom");
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].title, "Title 臺");
  EXPECT_EQ(items[0].url, "https://a/1");
  EXPECT_EQ(items[0].published_at, "2026-11-28T10:00:00Z");
  std::string error;
  auto lines = news::ParseJsonLines("{\"title\":\"x\",\"url\":\"u\"}\nnot json\n", &error);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_FALSE(error.empty());
}

class NewsDataTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string error;
    ASSERT_TRUE(data_.Load("data/election/2026", &error)) << error;
    refs_ = news::CandidateRefs(data_);
  }
  election::ElectionData data_;
  std::vector<news::CandidateRef> refs_;
};

TEST_F(NewsDataTest, MentionsAndHeuristic) {
  store::Article a;
  a.title = "最新民調：沈伯洋支持度上升，蔣萬安陣營低調";
  auto mentioned = news::MentionedCandidates(a, refs_);
  ASSERT_EQ(mentioned.size(), 2u);
  news::HeuristicClassifier h;
  news::ClassifyResult r;
  std::string error;
  ASSERT_TRUE(h.Classify(a, mentioned, &r, &error));
  std::map<std::string, Sentiment> by;
  for (const auto& x : r.assessments) by[x.candidate_id] = x.sentiment;
  EXPECT_EQ(by["63000-02"], Sentiment::kGood);

  store::Article b;
  b.title = "開票：蔣萬安 反超 沈伯洋";
  mentioned = news::MentionedCandidates(b, refs_);
  ASSERT_TRUE(h.Classify(b, mentioned, &r, &error));
  by.clear();
  for (const auto& x : r.assessments) by[x.candidate_id] = x.sentiment;
  EXPECT_EQ(by["63000-05"], Sentiment::kGood);
  EXPECT_EQ(by["63000-02"], Sentiment::kBad);
}

TEST_F(NewsDataTest, LlmRequestFollowsOpenAiChatProtocol) {
  store::Article a;
  a.title = "沈伯洋 造勢";
  a.summary = "text";
  news::LlmConfig cfg;
  cfg.model = "gpt-test";
  const auto mentioned = news::MentionedCandidates(a, refs_);
  const auto req = nlohmann::json::parse(news::LlmClassifier::BuildRequest(cfg, a, mentioned));
  EXPECT_EQ(req["model"], "gpt-test");
  EXPECT_EQ(req["response_format"]["type"], "json_object");
  ASSERT_EQ(req["messages"].size(), 2u);
  EXPECT_EQ(req["messages"][0]["role"], "system");
  EXPECT_NE(req["messages"][1]["content"].get<std::string>().find("63000-02"), std::string::npos);
}

TEST_F(NewsDataTest, LlmResponseParsing) {
  const auto mentioned = news::MentionedCandidates(store::Article{0, "", "", "沈伯洋 蔣萬安"}, refs_);
  const std::string answer = R"({"summary":"摘要","assessments":[
      {"candidate_id":"63000-02","sentiment":"Good","reason":"r"},
      {"candidate_id":"99999-01","sentiment":"bad"},
      {"candidate_id":"63000-05","sentiment":"weird"}]})";
  const nlohmann::json body = {
      {"model", "gpt-x"},
      {"choices", {{{"message", {{"role", "assistant"}, {"content", "```json\n" + answer + "\n```"}}}}}}};
  news::ClassifyResult r;
  std::string error;
  ASSERT_TRUE(news::LlmClassifier::ParseResponse(body.dump(), mentioned, &r, &error)) << error;
  EXPECT_EQ(r.digest, "摘要");
  EXPECT_EQ(r.model, "gpt-x");
  ASSERT_EQ(r.assessments.size(), 1u);  // unknown id and bad label dropped
  EXPECT_EQ(r.assessments[0].sentiment, Sentiment::kGood);
  EXPECT_FALSE(news::LlmClassifier::ParseResponse(R"({"error":{"message":"quota"}})", mentioned, &r,
                                                  &error));
  EXPECT_NE(error.find("quota"), std::string::npos);
}

TEST_F(NewsDataTest, MockNewsIsLabelledAndDeterministic) {
  news::MockNewsGenerator a(&data_, 1), b(&data_, 1);
  const auto x = a.Batch(50), y = b.Batch(50);
  ASSERT_EQ(x.size(), 50u);
  std::set<Sentiment> seen;
  for (size_t i = 0; i < x.size(); ++i) {
    EXPECT_EQ(x[i].article.title, y[i].article.title);
    EXPECT_EQ(x[i].article.title.rfind("【模擬】", 0), 0u);
    EXPECT_NE(x[i].article.summary.find("非真實報導"), std::string::npos);
    EXPECT_TRUE(x[i].article.simulated);
    EXPECT_EQ(x[i].article.url.rfind("mock://", 0), 0u);
    seen.insert(x[i].intended);
    // The subject is always found by the mention detector.
    bool found = false;
    for (const auto& c : news::MentionedCandidates(x[i].article, refs_)) found |= c.id == x[i].candidate_id;
    EXPECT_TRUE(found) << x[i].article.title;
  }
  EXPECT_EQ(seen.size(), 3u);
  EXPECT_FALSE(a.Between(0, 600, 30).empty());
}

TEST_F(NewsDataTest, ServiceStoresSubmittedAndMockArticles) {
  news::NewsConfig cfg;
  cfg.use_llm = false;
  news::NewsService service(cfg, refs_);
  store::Database db;
  std::string error;
  ASSERT_TRUE(db.Open(":memory:", &error));
  news::MockNewsGenerator gen(&data_, 3);
  for (auto& m : gen.Batch(10)) service.SubmitArticle(m.article);
  service.Submit(news::ArticleFromEvent("臺北市長：沈伯洋 宣布勝選", "sim://x", "t",
                                        {{"63000-02", Sentiment::kGood, ""}}, true));
  service.RunOnce(&db);
  EXPECT_EQ(db.LatestNews(100).size(), 11u);
  EXPECT_TRUE(db.UnclassifiedArticles(10).empty());
  EXPECT_GE(db.TotalCounts().total(), 11);
}

}  // namespace
}  // namespace twn
