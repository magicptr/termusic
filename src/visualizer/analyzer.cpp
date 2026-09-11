#include "visualizer/analyzer.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <fftw3.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>
#include <optional>

namespace termusic {
namespace {

constexpr std::size_t kFftSize = 2048;

} // namespace

struct VisualizerAnalyzer::FftPlan {
  FftPlan() {
    input = static_cast<float *>(fftwf_malloc(sizeof(float) * kFftSize));
    output = static_cast<fftwf_complex *>(
        fftwf_malloc(sizeof(fftwf_complex) * (kFftSize / 2U + 1U)));
    if (input != nullptr && output != nullptr) {
      plan = fftwf_plan_dft_r2c_1d(static_cast<int>(kFftSize), input, output,
                                   FFTW_ESTIMATE);
    }
  }

  ~FftPlan() {
    if (plan != nullptr)
      fftwf_destroy_plan(plan);
    fftwf_free(output);
    fftwf_free(input);
  }

  float *input = nullptr;
  fftwf_complex *output = nullptr;
  fftwf_plan plan = nullptr;
};

VisualizerAnalyzer::VisualizerAnalyzer()
    : fft_plan_(std::make_unique<FftPlan>()) {}
VisualizerAnalyzer::~VisualizerAnalyzer() { stop(); }

void VisualizerAnalyzer::start(std::string fifo_path, int sample_rate,
                               int channels, int bands, float sensitivity,
                               int refresh_hz, UpdateCallback callback) {
  stop();
  fifo_path_ = std::move(fifo_path);
  sample_rate_ = std::clamp(sample_rate, 8000, 192000);
  channels_ = std::clamp(channels, 1, 8);
  bands_ = std::clamp(bands, 32, 96);
  refresh_hz_ = std::clamp(refresh_hz, 5, 60);
  sensitivity_.store(std::clamp(sensitivity, 0.1F, 5.0F));
  running_peak_ = 0.0F;
  last_frame_time_ = {};
  {
    std::scoped_lock lock(mutex_);
    ensureBandCount(static_cast<std::size_t>(bands_));
    snapshot_.data_available = false;
  }
  reader_thread_ = std::jthread([this, callback = std::move(callback)](
                                    std::stop_token stop_token) mutable {
    readerLoop(stop_token, std::move(callback));
  });
}

void VisualizerAnalyzer::requestStop() {
  if (reader_thread_.joinable())
    reader_thread_.request_stop();
}

void VisualizerAnalyzer::stop() {
  requestStop();
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
}

void VisualizerAnalyzer::setPlaybackActive(bool active) {
  playback_active_.store(active);
}

void VisualizerAnalyzer::setSensitivity(float sensitivity) {
  sensitivity_.store(std::clamp(sensitivity, 0.1F, 5.0F));
}

SpectrumSnapshot VisualizerAnalyzer::snapshot() const {
  std::scoped_lock lock(mutex_);
  return snapshot_;
}

void VisualizerAnalyzer::ensureBandCount(std::size_t bands) {
  // Two distinct situations, deliberately handled differently:
  //
  //  * The band count genuinely changed -> allocate all companions together.
  //  * The band count is unchanged -> every companion MUST already match. If
  //    one drifted, that is broken internal state, not something to paper over
  //    by wiping the others. Assert loudly in Debug instead of silently
  //    resetting good state.
  if (allocated_bands_ == bands) {
    assert(snapshot_.bars.size() == bands);
    assert(snapshot_.peaks.size() == bands);
    assert(peak_hold_.size() == bands);
    return;
  }
  snapshot_.bars.assign(bands, 0.0F);
  snapshot_.peaks.assign(bands, 0.0F);
  peak_hold_.assign(bands, 0.0F);
  allocated_bands_ = bands;
  ++band_state_resets_;
}

void VisualizerAnalyzer::analyzePcm(
    std::span<const std::int16_t> interleaved_samples) {
  const std::size_t channels = static_cast<std::size_t>(channels_);
  if (interleaved_samples.size() < kFftSize * channels)
    return;
  if (fft_plan_ == nullptr || fft_plan_->plan == nullptr)
    return;

  for (std::size_t frame = 0; frame < kFftSize; ++frame) {
    float mono = 0.0F;
    for (std::size_t channel = 0; channel < channels; ++channel) {
      mono +=
          static_cast<float>(interleaved_samples[frame * channels + channel]);
    }
    mono /= static_cast<float>(channels) * 32768.0F;
    const float window =
        0.5F - 0.5F * std::cos(2.0F * std::numbers::pi_v<float> *
                               static_cast<float>(frame) /
                               static_cast<float>(kFftSize - 1U));
    fft_plan_->input[frame] = mono * window;
  }
  fftwf_execute(fft_plan_->plan);

  std::vector<float> levels(static_cast<std::size_t>(bands_), 0.0F);
  const float nyquist = static_cast<float>(sample_rate_) / 2.0F;
  const float max_frequency = std::min(18000.0F, nyquist);
  const float min_frequency = 40.0F;
  for (int band = 0; band < bands_; ++band) {
    const float start_ratio =
        static_cast<float>(band) / static_cast<float>(bands_);
    const float end_ratio =
        static_cast<float>(band + 1) / static_cast<float>(bands_);
    const float low =
        min_frequency * std::pow(max_frequency / min_frequency, start_ratio);
    const float high =
        min_frequency * std::pow(max_frequency / min_frequency, end_ratio);
    const auto low_bin = std::clamp<std::size_t>(
        static_cast<std::size_t>(low * static_cast<float>(kFftSize) /
                                 static_cast<float>(sample_rate_)),
        1U, kFftSize / 2U - 1U);
    const auto high_bin = std::clamp<std::size_t>(
        static_cast<std::size_t>(high * static_cast<float>(kFftSize) /
                                 static_cast<float>(sample_rate_)),
        low_bin + 1U, kFftSize / 2U);
    float power = 0.0F;
    for (std::size_t bin = low_bin; bin < high_bin; ++bin) {
      const float real = fft_plan_->output[bin][0];
      const float imaginary = fft_plan_->output[bin][1];
      power += real * real + imaginary * imaginary;
    }
    power /= static_cast<float>(high_bin - low_bin);
    // FFTW output is unnormalised. Restore an approximate signal amplitude
    // and apply a soft knee so loud input keeps moving instead of clipping.
    const float amplitude =
        std::sqrt(power) * (2.0F / static_cast<float>(kFftSize)) *
        sensitivity_.load();
    // dB mapping rather than a linear soft knee: weak bands keep visible
    // variation and strong bands stop pinning to the ceiling.
    constexpr float kEpsilon = 1e-9F;
    constexpr float kNoiseFloorDb = -72.0F;
    constexpr float kCeilingDb = -10.0F;
    const float db = 20.0F * std::log10(amplitude + kEpsilon);
    const float normalized =
        std::clamp((db - kNoiseFloorDb) / (kCeilingDb - kNoiseFloorDb), 0.0F,
                   1.0F);
    levels[static_cast<std::size_t>(band)] =
        std::pow(normalized, 0.62F);
  }

  // Slow adaptive gain: track a high percentile of the frame and normalise
  // against it. Rise is immediate so a loud passage is never clipped away;
  // the fall is very slow (~5 s), which is what stops the whole spectrum from
  // visibly breathing in and out.
  const auto percentile = [&levels](float fraction) {
    if (levels.empty())
      return 0.0F;
    std::vector<float> sorted(levels);
    const auto index = static_cast<std::size_t>(
        fraction * static_cast<float>(sorted.size() - 1));
    std::nth_element(sorted.begin(),
                     sorted.begin() + static_cast<std::ptrdiff_t>(index),
                     sorted.end());
    return sorted[index];
  };
  const float frame_peak = percentile(0.95F);
  running_peak_ = std::max(frame_peak, running_peak_ * 0.995F);
  const float gain = 1.0F / std::max(0.30F, running_peak_);
  for (float &level : levels)
    level = std::clamp(level * gain, 0.0F, 1.0F);

  // Real elapsed time between analysis frames.
  const auto frame_now = std::chrono::steady_clock::now();
  float frame_dt = 1.0F / 60.0F;
  if (last_frame_time_.time_since_epoch().count() != 0)
    frame_dt = static_cast<float>(
        std::chrono::duration<double>(frame_now - last_frame_time_).count());
  last_frame_time_ = frame_now;
  frame_dt = std::clamp(frame_dt, 1.0F / 240.0F, 0.20F);

  std::scoped_lock lock(mutex_);
  ensureBandCount(levels.size());
  // The invariant is now structural: every companion container is sized by the
  // same call, so no loop can index a shorter companion.
  assert(snapshot_.bars.size() == levels.size());
  assert(snapshot_.peaks.size() == levels.size());
  assert(peak_hold_.size() == levels.size());
  // Smoothing is TIME-based, not per-frame. The analysis rate depends on how
  // the FIFO delivers audio (reads arrive in bursts), so a fixed per-frame
  // coefficient over-smooths fast material into a plateau -- a 160 BPM kick
  // pattern would read as a sustained tone and lose its onsets entirely.
  const float attack_alpha =
      1.0F - std::exp(-frame_dt / 0.020F); // 20 ms
  const float release_alpha =
      1.0F - std::exp(-frame_dt / 0.110F); // 110 ms
  for (std::size_t index = 0; index < levels.size(); ++index) {
    const float old = snapshot_.bars[index];
    const float coefficient =
        levels[index] > old ? attack_alpha : release_alpha;
    snapshot_.bars[index] = old + (levels[index] - old) * coefficient;
    // Peak hold + dt-based decay, so the fall rate no longer depends on the
    // analyzer frame rate.
    if (snapshot_.bars[index] >= snapshot_.peaks[index]) {
      snapshot_.peaks[index] = snapshot_.bars[index];
      peak_hold_[index] = 0.22F; // 220 ms
    } else if (peak_hold_[index] > 0.0F) {
      peak_hold_[index] -= frame_dt;
    } else {
      snapshot_.peaks[index] =
          std::max(snapshot_.bars[index],
                   snapshot_.peaks[index] - 0.70F * frame_dt);
    }
  }
  snapshot_.data_available = true;
}

bool VisualizerAnalyzer::companionSizesConsistent() const {
  std::scoped_lock lock(mutex_);
  return snapshot_.bars.size() == allocated_bands_ &&
         snapshot_.peaks.size() == allocated_bands_ &&
         peak_hold_.size() == allocated_bands_;
}

#ifndef NDEBUG
void VisualizerAnalyzer::debugCorruptCompanion() {
  std::scoped_lock lock(mutex_);
  // Simulates the Round 17 defect: one companion silently emptied while the
  // allocated band count still claims otherwise.
  peak_hold_.clear();
}
#endif

void VisualizerAnalyzer::decay(bool mark_unavailable) {
  std::scoped_lock lock(mutex_);
  // The loop is bounded by the shortest companion, and the invariant is
  // asserted, so bars/peaks can never be indexed out of range here.
  assert(snapshot_.bars.size() == snapshot_.peaks.size());
  const std::size_t bands =
      std::min(snapshot_.bars.size(), snapshot_.peaks.size());
  for (std::size_t index = 0; index < bands; ++index) {
    snapshot_.bars[index] *= 0.82F;
    snapshot_.peaks[index] =
        std::max(snapshot_.bars[index], snapshot_.peaks[index] - 0.035F);
  }
  if (mark_unavailable)
    snapshot_.data_available = false;
}

void VisualizerAnalyzer::readerLoop(std::stop_token stop,
                                    UpdateCallback callback) {
  std::vector<std::int16_t> samples;
  samples.reserve(kFftSize * static_cast<std::size_t>(channels_) * 2U);
  std::vector<std::uint8_t> bytes(16384U);
  auto last_data = std::chrono::steady_clock::now();
  auto last_analysis = last_data - std::chrono::seconds(1);

  while (!stop.stop_requested()) {
    const int descriptor = ::open(fifo_path_.c_str(), O_RDONLY | O_NONBLOCK);
    if (descriptor < 0) {
      decay(true);
      if (callback)
        callback();
      for (int retry = 0; retry < 4 && !stop.stop_requested(); ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(125));
      }
      continue;
    }

    // read(2) is allowed to split a 16-bit PCM sample at any byte boundary.
    // Keep an orphaned low byte until the next read instead of dropping it
    // and shifting every later sample by eight bits.
    std::optional<std::uint8_t> pending_low_byte;

    while (!stop.stop_requested()) {
      pollfd poll_descriptor{descriptor, POLLIN, 0};
      const int poll_result = ::poll(&poll_descriptor, 1, 100);
      if (poll_result < 0 ||
          (poll_descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
        break;
      }
      if ((poll_descriptor.revents & POLLIN) != 0) {
        const ssize_t count = ::read(descriptor, bytes.data(), bytes.size());
        if (count > 0) {
          last_data = std::chrono::steady_clock::now();
          const std::size_t received = static_cast<std::size_t>(count);
          std::size_t offset = 0;
          if (pending_low_byte && received > 0U) {
            const auto high = static_cast<std::uint16_t>(bytes[0]);
            samples.push_back(static_cast<std::int16_t>(
                static_cast<std::uint16_t>(*pending_low_byte) | (high << 8U)));
            pending_low_byte.reset();
            offset = 1;
          }
          for (; offset + 1U < received; offset += 2U) {
            const auto low = static_cast<std::uint16_t>(bytes[offset]);
            const auto high = static_cast<std::uint16_t>(bytes[offset + 1U]);
            samples.push_back(static_cast<std::int16_t>(low | (high << 8U)));
          }
          if (offset < received)
            pending_low_byte = bytes[offset];
        }
      }

      const std::size_t window_samples =
          kFftSize * static_cast<std::size_t>(channels_);
      const auto now = std::chrono::steady_clock::now();
      const auto interval = std::chrono::milliseconds(1000 / refresh_hz_);
      if (playback_active_.load() && samples.size() >= window_samples &&
          now - last_analysis >= interval) {
        analyzePcm(
            std::span<const std::int16_t>(samples.data(), window_samples));
        last_analysis = now;
        const std::size_t hop =
            std::min(window_samples,
                     static_cast<std::size_t>(sample_rate_ / refresh_hz_) *
                         static_cast<std::size_t>(channels_));
        samples.erase(samples.begin(),
                      samples.begin() + static_cast<std::ptrdiff_t>(hop));
        if (callback)
          callback();
      } else if (!playback_active_.load() ||
                 now - last_data > std::chrono::seconds(1)) {
        samples.clear();
        decay(now - last_data > std::chrono::seconds(1));
        if (callback)
          callback();
      }
      if (samples.size() > window_samples * 4U) {
        samples.erase(samples.begin(),
                      samples.end() -
                          static_cast<std::ptrdiff_t>(window_samples));
      }

      if ((poll_descriptor.revents & POLLHUP) != 0)
        break;
    }
    ::close(descriptor);
    if (!stop.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

} // namespace termusic
