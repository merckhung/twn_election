#include "src/app/music.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#ifdef __linux__
#include <alsa/asoundlib.h>
#endif

namespace twn::app {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 44100.0;
constexpr double kBeatSeconds = 60.0 / 144.0;
constexpr double kBarSeconds = kBeatSeconds * 4.0;
constexpr double kChordHz[4][4] = {
    {146.83, 220.00, 293.66, 369.99},  // D major
    {123.47, 185.00, 246.94, 293.66},  // B minor
    {98.00, 146.83, 196.00, 246.94},   // G major
    {110.00, 164.81, 220.00, 329.63},  // A major
};
constexpr double kMelodyHz[8] = {587.33, 493.88, 440.00, 369.99,
                                 440.00, 493.88, 739.99, 587.33};

double Sine(double hz, double seconds) {
  return std::sin(2.0 * kPi * hz * seconds);
}

double Noise(int64_t sample) {
  uint64_t x = static_cast<uint64_t>(sample) + 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  x ^= x >> 31;
  return static_cast<double>(static_cast<int32_t>(x >> 32)) / 2147483648.0;
}

}  // namespace

AmbientMusic::~AmbientMusic() { Stop(); }

bool AmbientMusic::Start() {
#ifdef __linux__
  if (pcm_) return true;
  int err = snd_pcm_open(&pcm_, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (err >= 0) err = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_FLOAT_LE,
      SND_PCM_ACCESS_RW_INTERLEAVED, 2, static_cast<unsigned int>(kRate), 1, 100000);
  if (err < 0) {
    std::fprintf(stderr, "twn_election: background music unavailable: %s\n", snd_strerror(err));
    if (pcm_) snd_pcm_close(pcm_);
    pcm_ = nullptr;
    return false;
  }
  running_.store(true);
  thread_ = std::thread(&AmbientMusic::Render, this);
  return true;
#else
  return false;
#endif
}

void AmbientMusic::Stop() {
  running_.store(false);
  if (thread_.joinable()) thread_.join();
#ifdef __linux__
  if (pcm_) {
    snd_pcm_drain(pcm_);
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
  }
#endif
}

void AmbientMusic::Toggle() { enabled_.store(!enabled_.load()); }

void AmbientMusic::Render() {
  constexpr unsigned long kFrames = 512;
  float output[kFrames * 2];
  while (running_.load()) {
    RenderSamples(output, kFrames);
#ifdef __linux__
    const snd_pcm_sframes_t written = snd_pcm_writei(pcm_, output, kFrames);
    if (written < 0) snd_pcm_recover(pcm_, static_cast<int>(written), 1);
#endif
  }
}

void AmbientMusic::RenderSamples(float* output, unsigned long frames) {
  const float target = enabled_.load() ? 1.0f : 0.0f;
  for (unsigned long i = 0; i < frames; ++i, sample_ += 1.0) {
    const double t = sample_ / kRate;
    const double bar_pos = std::fmod(t, kBarSeconds * 4.0);
    const int chord = static_cast<int>(bar_pos / kBarSeconds);
    const double in_bar = std::fmod(bar_pos, kBarSeconds);
    const double fade = std::clamp(std::min(in_bar / 0.7, (kBarSeconds - in_bar) / 0.7), 0.0, 1.0);

    // Soft sustained chord with a slow breath and gentle octave shimmer.
    double pad = 0;
    for (int n = 0; n < 4; ++n) {
      const double hz = kChordHz[chord][n];
      pad += Sine(hz, t) * 0.58 + Sine(hz * 2.003, t) * 0.12;
    }
    const double breath = 0.82 + 0.18 * std::sin(2.0 * kPi * t / 8.0);
    pad *= fade * breath / 4.0;

    // A bright pentatonic hook repeats over the faster groove.
    const int note = static_cast<int>(t / kBeatSeconds) % 8;
    const double note_age = std::fmod(t, kBeatSeconds);
    const double pluck = std::exp(-note_age * 3.6) *
        (Sine(kMelodyHz[note], t) * 0.75 + Sine(kMelodyHz[note] * 2.76, t) * 0.14);
    const int beat_index = static_cast<int>(t / kBeatSeconds) % 4;
    const double beat_age = std::fmod(t, kBeatSeconds);

    // A syncopated low bass line fills the spaces between the kick hits.
    const double eighth = kBeatSeconds * 0.5;
    const int eighth_index = static_cast<int>(t / eighth) % 8;
    const double bass_age = std::fmod(t, eighth);
    constexpr double kBassAccent[8] = {1.0, 0.0, 0.62, 0.0, 0.92, 0.0, 0.62, 0.72};
    const double bass_hz = kChordHz[chord][0] * (eighth_index == 2 || eighth_index == 6 ? 0.75 : 0.5);
    const double bass = kBassAccent[eighth_index] * std::exp(-bass_age * 4.5) *
        (Sine(bass_hz, bass_age) * 0.8 + Sine(bass_hz * 2.0, bass_age) * 0.15);

    // Bright chord stabs and a rolling chord-tone arpeggio add harmonic movement.
    const double stab = (beat_index == 0 || beat_index == 2)
        ? std::exp(-beat_age * 4.2) *
              (Sine(kChordHz[chord][1] * 2.0, beat_age) +
               Sine(kChordHz[chord][3] * 2.0, beat_age) * 0.65)
        : 0.0;
    constexpr int kArpNotes[8] = {0, 1, 2, 1, 3, 2, 1, 2};
    const double arp_hz = kChordHz[chord][kArpNotes[eighth_index]] * 2.0;
    const double arpeggio = std::exp(-bass_age * 7.0) *
        (Sine(arp_hz, t) * 0.8 + Sine(arp_hz * 2.01, t) * 0.12);

    // Four-on-the-floor kick, backbeat snare, and crisp eighth-note hats.
    double drums = 0;
    if (beat_index == 0 || beat_index == 2) {
      const double kick_phase = 2.0 * kPi * (48.0 * beat_age +
          72.0 * (1.0 - std::exp(-24.0 * beat_age)) / 24.0);
      drums += 0.30 * std::sin(kick_phase) * std::exp(-beat_age * 8.5);
    }
    if (beat_index == 1 || beat_index == 3) {
      const int64_t sample_index = static_cast<int64_t>(sample_);
      const double snare = Noise(sample_index) - Noise(sample_index - 1);
      drums += (0.075 * snare + 0.045 * Sine(185.0, beat_age)) * std::exp(-beat_age * 16.0);
    }
    const double hat_age = std::fmod(t, eighth);
    const double hat_accent = (static_cast<int>(t / eighth) % 4 == 0) ? 1.0 : 0.65;
    const int64_t sample_index = static_cast<int64_t>(sample_);
    const double hat_noise = Noise(sample_index) - Noise(sample_index - 1);
    drums += 0.020 * hat_accent * hat_noise * std::exp(-hat_age * 115.0);

    gain_ += (target - gain_) * 0.00018f;
    const float sample = static_cast<float>(
        (pad * 0.10 + pluck * 0.055 + bass * 0.19 + stab * 0.035 + arpeggio * 0.025 + drums) * gain_);
    output[2 * i] = sample;
    output[2 * i + 1] = sample * 0.96f;
  }
}

}  // namespace twn::app
