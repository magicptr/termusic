#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace termusic {

struct SpectrumSnapshot {
  std::vector<float> bars;
  std::vector<float> peaks;
  bool data_available = false;
};

/// Reads signed 16-bit little-endian interleaved PCM from an MPD FIFO output
/// and publishes a smoothed, logarithmically-spaced spectrum. No synthetic
/// animation is generated when the FIFO is unavailable.
class VisualizerAnalyzer {
public:
  using UpdateCallback = std::function<void()>;

  VisualizerAnalyzer();
  ~VisualizerAnalyzer();
  VisualizerAnalyzer(const VisualizerAnalyzer &) = delete;
  VisualizerAnalyzer &operator=(const VisualizerAnalyzer &) = delete;

  void start(std::string fifo_path, int sample_rate, int channels, int bands,
             float sensitivity, int refresh_hz, UpdateCallback callback);
  /// Requests shutdown without waiting for the reader thread to finish.
  /// This lets the UI stop every worker concurrently before joining them.
  void requestStop();
  void stop();
  void setPlaybackActive(bool active);
  void setSensitivity(float sensitivity);
  SpectrumSnapshot snapshot() const;

  /// Public for deterministic tests and alternate PCM producers.
  void analyzePcm(std::span<const std::int16_t> interleaved_samples);

private:
  struct FftPlan;

  void readerLoop(std::stop_token stop, UpdateCallback callback);
  void decay(bool mark_unavailable);
  /// Sizes bars/peaks/peak_hold_ together. Caller must hold mutex_.
  void ensureBandCount(std::size_t bands);
  /// Increments only on a legitimate band-count change, never per frame.
  std::size_t band_state_resets_ = 0;

  mutable std::mutex mutex_;
  SpectrumSnapshot snapshot_;
  std::string fifo_path_;
  int sample_rate_ = 44100;
  int channels_ = 2;
  int bands_ = 64;
  int refresh_hz_ = 30;
  std::atomic<float> sensitivity_{1.0F};
  /// Slow AGC reference: rises immediately, decays over ~5 s.
  float running_peak_ = 0.0F;
  /// Remaining peak-hold time per band, in seconds.
  std::vector<float> peak_hold_;
  /// Band count the companion containers are currently allocated for. Used to
  /// tell a legitimate band-count change apart from internal size drift.
  std::size_t allocated_bands_ = 0;

public:
  /// True when bars/peaks/peak_hold_ all match the allocated band count.
  /// Test hook for the companion-drift regression.
  bool companionSizesConsistent() const;
  std::size_t allocatedBandCount() const { return allocated_bands_; }
  /// Reallocations of the companion containers; one per real band-count change.
  std::size_t bandStateResets() const { return band_state_resets_; }
#ifdef TERMUSIC_TEST_HOOKS
  /// Deliberately breaks the companion invariant so a test can prove the
  /// detector notices it. Never called by production code, and only compiled
  /// when the tests are built: a release build with BUILD_TESTING=ON must link
  /// them without turning the analyzer's own assertions on.
  void debugCorruptCompanion();
#endif

private:
  std::chrono::steady_clock::time_point last_frame_time_{};
  std::atomic<bool> playback_active_{false};
  std::unique_ptr<FftPlan> fft_plan_;
  std::jthread reader_thread_;
};

} // namespace termusic
