#include "visualizer/beat.hpp"

#include <algorithm>
#include <cmath>

namespace termusic {
namespace {

/// The tempo/onset constants. They are deliberately conservative: a missed
/// beat is invisible, a false one makes the axis twitch on noise.
constexpr double kOnsetRatio = 1.35;    // energy above this share of baseline
constexpr double kOnsetFloor = 0.04;    // ... and above this absolute level
constexpr double kRefractory = 0.15;    // s; ignores double triggers per hit
constexpr double kBaselineRise = 1.20;  // s; the baseline follows slowly
constexpr double kBaselineFall = 2.50;  // s
constexpr double kPrimingSeconds = 1.0; // s; a fast reference while starting up
constexpr double kPrimingTau = 0.35;    // s
constexpr double kEnvelopeTau = 0.22;   // s; the visible pulse decay
constexpr double kMinTempoInterval = 0.20;  // 300 BPM
constexpr double kMaxTempoInterval = 2.00;  // 30 BPM

} // namespace

double lowBandEnergy(const float *bands, int band_count, int count) {
  if (bands == nullptr || band_count <= 0 || count <= 0)
    return 0.0;
  const int usable = std::min(band_count, count);
  double sum = 0.0;
  for (int index = 0; index < usable; ++index)
    sum += std::clamp(static_cast<double>(bands[index]), 0.0, 1.0);
  return sum / static_cast<double>(usable);
}

BeatState beatStep(BeatState state, double low_energy, double dt) {
  const double step = std::clamp(dt, 0.0, 0.25);
  const double energy = std::clamp(low_energy, 0.0, 1.0);
  // The very first sample only establishes the reference: there is nothing to
  // compare against yet, so it can never be an onset.
  const bool first_sample = state.observed <= 0.0;

  // The envelope always decays; a beat sets it back to 1 below.
  state.envelope *= std::exp(-step / kEnvelopeTau);
  if (state.envelope < 1e-3)
    state.envelope = 0.0;

  // Baseline: a slow rolling average, faster up than down, so a sustained
  // loud passage is absorbed instead of firing a beat on every frame.
  state.observed += step;
  const double tau = state.observed < kPrimingSeconds
                         ? kPrimingTau
                         : (energy > state.baseline ? kBaselineRise
                                                    : kBaselineFall);
  const double alpha = 1.0 - std::exp(-step / tau);
  const bool first = state.baseline <= 0.0;
  state.baseline += (energy - state.baseline) * (first ? 1.0 : alpha);

  if (state.since_last >= 0.0)
    state.since_last += step;
  else
    state.since_last = 0.0;

  // Re-arm once the energy has come back near the reference. The threshold sits
  // clearly below the onset ratio, so a dense kick pattern can fire once per
  // hit while a single decaying hit still cannot trigger twice.
  if (!state.armed && energy <= state.baseline * 1.15)
    state.armed = true;
  const bool onset = !first_sample && state.armed && energy > kOnsetFloor &&
                     energy > state.baseline * kOnsetRatio &&
                     state.since_last >= kRefractory;
  if (!onset)
    return state;
  state.armed = false;

  // A beat. Keep the intervals that could be a tempo; a long gap means the
  // music stopped rather than that it is very slow.
  const double interval = state.since_last;
  state.envelope = 1.0;
  ++state.beats;
  state.since_last = 0.0;
  if (interval >= kMinTempoInterval && interval <= kMaxTempoInterval) {
    state.last_interval = interval;
    if (state.interval_count < BeatState::kHistory) {
      state.intervals[state.interval_count] = interval;
      ++state.interval_count;
    } else {
      for (int i = 1; i < BeatState::kHistory; ++i)
        state.intervals[i - 1] = state.intervals[i];
      state.intervals[BeatState::kHistory - 1] = interval;
    }
    if (state.interval_count >= 3) {
      double sum = 0.0;
      for (int i = 0; i < state.interval_count; ++i)
        sum += state.intervals[i];
      state.mean_interval = sum / static_cast<double>(state.interval_count);
      if (state.mean_interval > 0.0)
        state.bpm = 60.0 / state.mean_interval;
    }
  } else {
    // A gap that cannot be a tempo: start the tempo window again.
    state.last_interval = 0.0;
    state.mean_interval = 0.0;
    state.bpm = 0.0;
    state.interval_count = 0;
  }
  return state;
}

} // namespace termusic
