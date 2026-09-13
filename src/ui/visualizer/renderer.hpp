#pragma once

// The visualizer's renderer layer.
//
//   Analyzer (FIFO -> FFT -> bands)          src/visualizer/
//        |
//   spectrum / peaks / beat  ->  VisualizerFrame
//        |
//   Spectrum                                 src/ui/visualizer/spectrum.cpp
//        |
//   thin independent bars (one cell of bar, one of air), an absolute-height
//   vertical gradient, and a dot baseline that is always on screen
//
// ONE renderer exists, and it is not selected at runtime: there is no style id,
// no style registry and no factory switch any more. The Spectrum is what the
// application shows, everywhere the visualizer appears.

#include <memory>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "ui/theme.hpp"
#include "ui/visualizer/palette.hpp"

namespace termusic::ui {

/// The one visualizer's id. Diagnostics may say "spectrum"; nothing looks it up.
inline constexpr std::string_view kVisualizerId = "spectrum";

/// Everything the renderer needs for one frame, and nothing else. The
/// references point at application state that outlives the call; the renderer
/// must never store them, only read them while updating or drawing.
struct VisualizerFrame {
  /// Smoothed spectrum, one value per band, 0..1. Empty means "no data yet".
  const std::vector<float> &bands;
  /// The active grid, in cells: where the renderer is allowed to draw. It never
  /// recomputes an offset from the spectrum, which is what keeps the object
  /// still while it animates.
  int columns = 0;
  int rows = 0;
  /// Beat envelope, 0..1: 1.0 on the onset, decaying after it.
  float beat = 0.0F;
  /// Seconds since the previous update, already clamped by the caller.
  float dt = 1.0F / 60.0F;
  /// True while audio is actually arriving. Silence is a state the renderer
  /// must handle: the bars fall back to nothing while the dot baseline stays.
  bool live = false;
  const Theme &theme;
  const VisualizerPalette &palette;
};

/// What the renderer is currently holding. Diagnostics for tests and for the
/// scripted state line; every count stays bounded by the grid.
struct VisualizerStats {
  /// Bars the renderer could draw (0 when it has no state).
  int capacity = 0;
  /// Bars currently holding a non-zero height.
  int retained = 0;
  /// Cells the last frame actually drew: the bars plus the dot baseline.
  int drawn = 0;
  /// The row the bars stand on: the DOT row. The bars grow upward from the row
  /// above it, and the dots are drawn on it whatever the audio is doing.
  int baseline = 0;
  /// The tallest a bar may be, in rows above the dot row.
  int main_rows = 0;
  /// The bar count of the last frame, and the dot count -- the dots are one per
  /// bar, so the two are always equal.
  int bars = 0;
  int dots = 0;
};

/// The renderer interface. One implementation exists (the Spectrum); the
/// interface is kept because the Application owns it through a pointer and the
/// hidden test seam is worth more than the two lines it costs.
class VisualizerRenderer {
public:
  virtual ~VisualizerRenderer() = default;
  /// The renderer's id, for diagnostics: `kVisualizerId`.
  virtual std::string_view id() const = 0;
  /// Drops EVERY byte of runtime state: called when the visualizer settings
  /// change, so a reconfiguration can never inherit the previous spectrum.
  virtual void reset() = 0;
  /// Advances the renderer's own state by one frame.
  virtual void update(const VisualizerFrame &frame) = 0;
  /// Draws the current state. Must stay inside frame.columns x frame.rows.
  /// Non-const: the renderer caches its column heights and its run buffers,
  /// which is what keeps a frame's allocations bounded.
  virtual ftxui::Element render(const VisualizerFrame &frame) = 0;
  virtual VisualizerStats stats() const { return {}; }
};

/// The ONE renderer. No lookup, no fallback: the Spectrum is the visualizer.
std::unique_ptr<VisualizerRenderer> makeSpectrumRenderer();

/// The gradient colour of a bar cell whose height above the baseline is
/// `heightFraction` (0 = on the baseline, 1 = the top of the bar area).
///
/// The colour comes from the ABSOLUTE height, never from a bar's own height:
/// a short bar can only ever show the low end of the ramp, so its own top never
/// turns into the peak colour.
ftxui::Color spectrumColor(float heightFraction,
                           const VisualizerPalette &palette,
                           const Theme &theme);

} // namespace termusic::ui
