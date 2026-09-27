#pragma once

#include <atomic>
#include <thread>

typedef struct _snd_pcm snd_pcm_t;

namespace twn::app {

// A generated upbeat instrumental score for the election dashboard.
class AmbientMusic {
 public:
  AmbientMusic() = default;
  ~AmbientMusic();
  AmbientMusic(const AmbientMusic&) = delete;
  AmbientMusic& operator=(const AmbientMusic&) = delete;

  bool Start();
  void Stop();
  void Toggle();
  bool enabled() const { return enabled_.load(); }

 private:
  void Render();
  void RenderSamples(float* output, unsigned long frames);

  snd_pcm_t* pcm_ = nullptr;
  std::atomic<bool> enabled_{true};
  std::atomic<bool> running_{false};
  std::thread thread_;
  float gain_ = 0;
  double sample_ = 0;
};

}  // namespace twn::app
