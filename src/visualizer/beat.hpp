#pragma once

namespace termusic {

/// Rhythm information derived from the LOW end of the spectrum.
///
/// The Spectrum needs beat TIMING, not a tempo number: the onset accents the
/// axis, and the cadence of those pulses is what makes BPM visible. The
/// analyzer has no beat detector and no tempo estimator, so this
/// is the smallest layer that provides the timing: a rolling baseline of
/// low-band energy, an onset test against that baseline with a refractory
/// period, and an envelope that jumps to 1 on a beat and decays smoothly.
///
/// It is deliberately pure so the cadence can be measured in a test instead of
/// judged by eye.
struct BeatState {
  /// 1.0 on the step a beat is detected, decaying towards 0 afterwards.
  double envelope = 0.0;
  /// Rolling average of the low-band energy: the onset reference.
  double baseline = 0.0;
  /// Seconds observed so far. The baseline adapts quickly at the start (a
  /// track that begins with a kick must not raise the reference above itself)
  /// and slowly afterwards.
  double observed = 0.0;
  /// Beats detected so far.
  int beats = 0;
  /// Seconds since the last beat, or -1 before the first one.
  double since_last = -1.0;
  /// Schmitt-trigger state: after a beat the detector waits for the energy to
  /// fall back to the reference before it can fire again, so one hit is one
  /// pulse no matter how coarsely the caller samples.
  bool armed = true;
  /// The most recent interval and the mean of the recent ones (seconds).
  double last_interval = 0.0;
  double mean_interval = 0.0;
  /// Estimated tempo from those intervals; 0 until there is enough evidence.
  double bpm = 0.0;
  /// Recent intervals, newest last: the tempo window. Kept IN the state (not
  /// in a static) so the whole beat logic stays a pure function of its input.
  static constexpr int kHistory = 8;
  double intervals[kHistory] = {};
  int interval_count = 0;
};

/// One step of the beat envelope.
///
/// `low_energy` is the aggregate of the low bands, normalised to 0..1, and `dt`
/// the time since the previous step. Time comes in through `dt` alone, so the
/// cadence does not depend on frame rate. The returned state is the input state
/// advanced by one step.
BeatState beatStep(BeatState state, double low_energy, double dt);

/// Aggregate of the lowest `count` values of `bands`, which is what a kick
/// lives in. Empty input, or `count` of zero, yields 0.
double lowBandEnergy(const float *bands, int band_count, int count);

} // namespace termusic
