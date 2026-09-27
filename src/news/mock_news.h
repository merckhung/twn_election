// Synthetic campaign news for testing the news pipeline, the LLM classifier
// and the UI. Every item is labelled "【模擬】" (source "MOCK", url mock://...)
// and says it is not a real report. Templates are deliberately mild (rallies,
// endorsements, poll moves, criticism by rivals): no invented crimes or
// scandals about real people. Each item carries the sentiment it was written
// to express, so classifier accuracy can be measured (news_tool --eval).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/election/model.h"
#include "src/store/db.h"

namespace twn::news {

struct MockArticle {
  store::Article article;
  std::string candidate_id;      // subject of the item
  store::Sentiment intended;     // ground truth for the subject
};

class MockNewsGenerator {
 public:
  MockNewsGenerator(const election::ElectionData* data, uint64_t seed);

  // Items "published" in the half-open interval of simulated minutes
  // (after 16:00 on election day; negative = earlier). ~`per_hour` items per
  // simulated hour on average.
  std::vector<MockArticle> Between(double from_minute, double to_minute, double per_hour = 12);

  // `n` items (for tests / evaluation), independent of time.
  std::vector<MockArticle> Batch(int n);

 private:
  MockArticle Make(uint64_t h, double minute);

  const election::ElectionData* data_;
  uint64_t seed_;
  std::vector<std::pair<int, int>> pool_;  // (race, candidate) weighted by prominence
};

}  // namespace twn::news
