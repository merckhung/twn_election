// Decides, for each candidate an article is about, whether the news is good,
// bad or neutral for that candidate.
//
//  * LlmClassifier talks to any OpenAI-compatible Chat Completions endpoint
//    (OpenAI, Azure OpenAI, OpenRouter, Ollama, vLLM, LM Studio, llama.cpp
//    server, ...): POST {base_url}/chat/completions, JSON-only answer.
//  * HeuristicClassifier is an offline keyword/lexicon fallback so counts
//    still work without an API key (marked model = "heuristic").
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "src/election/model.h"
#include "src/store/db.h"

namespace twn::news {

struct CandidateRef {
  std::string id;
  std::string name_zh;
  std::string name_en;
  std::string party;       // party short name
  std::string race_title;  // "臺北市長"
};

std::vector<CandidateRef> CandidateRefs(const election::ElectionData& data);

// Candidates whose (Chinese or English) name appears in the article.
std::vector<CandidateRef> MentionedCandidates(const store::Article& a,
                                              const std::vector<CandidateRef>& all);

struct ClassifyResult {
  std::string digest;  // one-sentence summary
  std::string model;
  std::vector<store::Assessment> assessments;
};

class Classifier {
 public:
  virtual ~Classifier() = default;
  // `candidates` are the (pre-filtered) candidates to assess.
  virtual bool Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                        ClassifyResult* out, std::string* error) = 0;
  virtual std::string Name() const = 0;
};

class HeuristicClassifier : public Classifier {
 public:
  bool Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                ClassifyResult* out, std::string* error) override;
  std::string Name() const override { return "heuristic"; }
};

struct LlmConfig {
  std::string base_url = "https://api.openai.com/v1";
  std::string model = "gpt-4o-mini";
  std::string api_key;  // from $TWN_LLM_API_KEY or $OPENAI_API_KEY when empty
  double temperature = 0;
  bool json_mode = true;  // send response_format {"type": "json_object"}
  long timeout_s = 60;
};

// Fills api_key/base_url/model from the environment (TWN_LLM_API_KEY or
// OPENAI_API_KEY, TWN_LLM_BASE_URL or OPENAI_BASE_URL, TWN_LLM_MODEL) when
// they are not already set on the command line.
void ApplyLlmEnvironment(LlmConfig* config);

class LlmClassifier : public Classifier {
 public:
  explicit LlmClassifier(LlmConfig config) : config_(std::move(config)) {}
  bool Classify(const store::Article& a, const std::vector<CandidateRef>& candidates,
                ClassifyResult* out, std::string* error) override;
  std::string Name() const override { return config_.model; }

  // Exposed for tests.
  static std::string BuildRequest(const LlmConfig& config, const store::Article& a,
                                  const std::vector<CandidateRef>& candidates);
  static bool ParseResponse(const std::string& body, const std::vector<CandidateRef>& candidates,
                            ClassifyResult* out, std::string* error);

 private:
  LlmConfig config_;
};

}  // namespace twn::news
