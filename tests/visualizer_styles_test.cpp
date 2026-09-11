// The visualizer's shared layers, tested where they are decidable:
//
//   * the beat envelope (shared by every style),
//   * the STYLE registry (exactly three styles, and what a legacy id becomes),
//   * the PALETTE registry (orthogonal to the style),
//   * the renderers' runtime state: bounded by construction, cleared on reset,
//     and emptied by silence.
//
// The rendering itself is measured from the pixels by the live-check harness
// (build/livecheck/viz_matrix.sh), which is where geometry belongs.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "ui/visualizer/palette.hpp"
#include "ui/visualizer/renderer.hpp"
#include "visualizer/beat.hpp"

namespace {

using termusic::ui::CellGrid;
using termusic::ui::VisualizerFrame;
using termusic::ui::VisualizerPalette;
using termusic::ui::VisualizerRenderer;
using termusic::ui::VisualizerStats;
using termusic::ui::VisualizerStyle;

/// Feeds a click track (an impulse every `interval` seconds) through the beat
/// detector and returns the intervals it reported.
std::vector<double> detectIntervals(double interval, int beats) {
  termusic::BeatState state;
  const double dt = 1.0 / 120.0; // 120 Hz, like a busy UI frame
  std::vector<double> detected;
  int previous_beats = 0;
  const double total = interval * static_cast<double>(beats) + 1.0;
  for (double t = 0.0; t < total; t += dt) {
    // A kick: energy spikes for ~40 ms at each beat, near-silence between.
    const double phase = std::fmod(t, interval);
    const double low = phase < 0.04 ? 0.85 : 0.10;
    state = termusic::beatStep(state, low, dt);
    if (state.beats > previous_beats) {
      previous_beats = state.beats;
      if (state.last_interval > 0.0)
        detected.push_back(state.last_interval);
    }
  }
  return detected;
}

double mean(const std::vector<double> &values) {
  if (values.empty())
    return 0.0;
  double sum = 0.0;
  for (const double value : values)
    sum += value;
  return sum / static_cast<double>(values.size());
}

/// A frame with `bands` values, all set to `level`.
struct Fixture {
  std::vector<float> bands;
  std::vector<float> peaks;
  termusic::ui::Theme theme;
  const VisualizerPalette *palette = nullptr;

  VisualizerFrame frame(int columns, int rows, float beat, float dt,
                        bool live, float level) {
    bands.assign(96, level);
    peaks.assign(96, level);
    palette = &termusic::ui::visualizerPalette("theme");
    return VisualizerFrame{.bands = bands,
                           .peaks = peaks,
                           .columns = columns,
                           .rows = rows,
                           .beat = beat,
                           .dt = dt,
                           .live = live,
                           .theme = theme,
                           .palette = *palette};
  }
};

} // namespace

int main() {
  // --- Beat envelope (shared by every style) --------------------------------
  {
    termusic::BeatState state;
    // Silence never beats, and the envelope stays at zero.
    for (int i = 0; i < 200; ++i)
      state = termusic::beatStep(state, 0.0, 1.0 / 60.0);
    assert(state.beats == 0);
    assert(state.envelope == 0.0);
    std::cout << "beat: silence produces no pulses\n";
  }
  {
    // A slow fixture (~60 BPM) and a fast one (~180 BPM) must be told apart.
    const std::vector<double> slow = detectIntervals(1.0, 12);
    const std::vector<double> fast = detectIntervals(1.0 / 3.0, 24);
    assert(slow.size() >= 8);
    assert(fast.size() >= 16);
    const double slow_mean = mean(slow);
    const double fast_mean = mean(fast);
    assert(std::abs(slow_mean - 1.0) < 0.06);
    assert(std::abs(fast_mean - 1.0 / 3.0) < 0.03);
    std::cout << "beat: 60 BPM detected at "
              << static_cast<int>(slow_mean * 1000) << " ms, 180 BPM at "
              << static_cast<int>(fast_mean * 1000) << " ms (fast pulses faster)\n";
  }
  {
    // The pulse must not fire twice inside the refractory window, and it must
    // decay smoothly rather than snapping between two extremes.
    termusic::BeatState state;
    for (int i = 0; i < 20; ++i)
      state = termusic::beatStep(state, 0.10, 1.0 / 60.0);
    assert(state.beats == 0);
    state = termusic::beatStep(state, 0.90, 1.0 / 60.0);
    assert(state.beats == 1 && state.envelope == 1.0);
    const double first = state.envelope;
    state = termusic::beatStep(state, 0.9, 1.0 / 60.0);
    assert(state.beats == 1);       // still the same hit
    assert(state.envelope < first); // ... and it is decaying
    assert(state.envelope > 0.5);
    std::cout << "beat: one hit per onset, smooth decay\n";
  }

  // --- Style registry -------------------------------------------------------
  {
    const std::vector<VisualizerStyle> &styles =
        termusic::ui::visualizerStyles();
    assert(styles.size() == 3U && "exactly three styles are supported");
    std::set<std::string> ids;
    for (const VisualizerStyle &style : styles) {
      ids.insert(std::string(style.id));
      assert(!style.label.empty());
      assert(style.make != nullptr);
    }
    assert(ids.count("classic-bars") == 1U);
    assert(ids.count("waterfall") == 1U);
    assert(ids.count("particles") == 1U);
    // The removed renderer is NOT a style under any of its names.
    for (const char *legacy : {"city", "cyber-city", "skyline", "cybercity",
                               "buildings", ""}) {
      assert(!termusic::ui::isVisualizerStyle(legacy));
      assert(termusic::ui::normalizeVisualizerStyleId(legacy) ==
             "classic-bars");
    }
    assert(termusic::ui::defaultVisualizerStyleId() == "classic-bars");
    // A factory always answers with a renderer, for any input at all.
    for (const char *requested : {"classic-bars", "waterfall", "particles",
                                  "city", "nonsense", ""}) {
      std::unique_ptr<VisualizerRenderer> renderer =
          termusic::ui::makeVisualizerRenderer(requested);
      assert(renderer != nullptr);
      assert(termusic::ui::isVisualizerStyle(renderer->id()));
    }
    std::cout << "styles: " << styles.size()
              << " registered (Classic Bars, Waterfall, Particles), legacy "
                 "ids normalize to classic-bars\n";
  }

  // --- Palette registry (independent of the style) --------------------------
  {
    const std::vector<VisualizerPalette> &palettes =
        termusic::ui::visualizerPalettes();
    assert(palettes.size() >= 2U);
    termusic::ui::Theme theme;
    for (const VisualizerPalette &palette : palettes) {
      assert(!palette.id.empty() && !palette.label.empty());
      assert(palette.ramp != nullptr);
      // Every ramp answers for the whole range, and an unknown id still
      // resolves to a real palette.
      for (const float intensity : {0.0F, 0.5F, 1.0F})
        (void)palette.ramp(intensity, theme);
      assert(termusic::ui::isVisualizerPalette(palette.id));
    }
    assert(termusic::ui::normalizeVisualizerPaletteId("nonsense") == "theme");
    std::cout << "palettes: " << palettes.size()
              << " registered, independent of the style\n";
  }

  // --- Every style: bounded state, cleared by reset, emptied by silence -----
  for (const VisualizerStyle &style : termusic::ui::visualizerStyles()) {
    Fixture fixture;
    std::unique_ptr<VisualizerRenderer> renderer = style.make();
    const int columns = 61;
    const int rows = 17;

    // 1. A loud signal: the style holds something and draws something.
    VisualizerStats loud;
    for (int frame_index = 0; frame_index < 120; ++frame_index) {
      VisualizerFrame frame =
          fixture.frame(columns, rows, 0.8F, 1.0F / 60.0F, true, 0.85F);
      renderer->update(frame);
      (void)renderer->render(frame);
      loud = renderer->stats();
    }
    assert(loud.drawn > 0 && "a loud signal must be visible");
    assert(loud.retained <= loud.capacity + columns &&
           "runtime state must stay bounded");

    // 2. Silence: nothing may keep drawing audio. Classic Bars keeps its
    //    baseline row (its structural floor), the others clear completely.
    VisualizerStats quiet;
    for (int frame_index = 0; frame_index < 240; ++frame_index) {
      VisualizerFrame frame =
          fixture.frame(columns, rows, 0.0F, 1.0F / 60.0F, false, 0.0F);
      renderer->update(frame);
      (void)renderer->render(frame);
      quiet = renderer->stats();
    }
    const bool bars = style.id == "classic-bars";
    assert(quiet.drawn <= (bars ? columns : 0) &&
           "silence must clear what audio drew");

    // 3. Reset: a style switch starts from nothing, so the next style can never
    //    inherit this one's state.
    renderer->reset();
    const VisualizerStats fresh = renderer->stats();
    assert(fresh.retained == 0);
    assert(fresh.drawn == 0);

    // 4. Resizing repeatedly must not grow anything.
    for (int step = 0; step < 40; ++step) {
      const int width = 20 + (step * 7) % 80;
      const int height = 5 + (step * 3) % 30;
      VisualizerFrame frame = fixture.frame(width, height, 0.5F, 1.0F / 60.0F,
                                            true, 0.7F);
      renderer->update(frame);
      (void)renderer->render(frame);
      const VisualizerStats stats = renderer->stats();
      assert(stats.retained <= stats.capacity + width);
      assert(stats.drawn <= width * height);
    }
    std::cout << "style " << style.id << ": loud " << loud.drawn
              << " cells drawn, silent " << quiet.drawn << " cells, capacity "
              << loud.capacity << ", reset clears\n";
  }

  // --- The cell grid is a bounded buffer, not a growing one -----------------
  {
    CellGrid grid;
    grid.resize(10, 4);
    assert(grid.columns() == 10 && grid.rows() == 4);
    grid.put(3, 1, "\u25aa", 0.5F);
    assert(grid.filled(3, 1) && grid.filledCells() == 1);
    // Out-of-range writes are dropped, never clamped onto a neighbour.
    grid.put(-1, 0, "\u2588", 1.0F);
    grid.put(10, 0, "\u2588", 1.0F);
    grid.put(0, 4, "\u2588", 1.0F);
    assert(grid.filledCells() == 1);
    grid.resize(2, 2);
    assert(grid.filledCells() == 0);
    std::cout << "grid: bounded, out-of-range writes dropped\n";
  }

  std::cout << "visualizer styles: all checks passed\n";
  return 0;
}
