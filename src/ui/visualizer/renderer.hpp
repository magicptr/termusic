#pragma once

// The visualizer's renderer layer.
//
//   Analyzer (FIFO -> FFT -> bands)          src/visualizer/
//        |
//   spectrum / peaks / beat  ->  VisualizerFrame
//        |
//   +----+-------------+-------------+
//   v                  v             v
//   Classic Bars   Waterfall     Particles      src/ui/visualizer/
//        \              |             /
//         \--------- Palette --------/
//
// The immersive UI asks the ACTIVE renderer to update and draw itself. It never
// branches on the style: adding a style is a new file plus one registry entry.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "ui/theme.hpp"
#include "ui/visualizer/palette.hpp"

namespace termusic::ui {

/// Everything a style needs for one frame, and nothing else. The references
/// point at application state that outlives the call; a style must never store
/// them, only read them while updating or drawing.
struct VisualizerFrame {
  /// Smoothed spectrum, one value per band, 0..1. Empty means "no data yet".
  const std::vector<float> &bands;
  /// Peak-hold per band, same indexing as `bands`.
  const std::vector<float> &peaks;
  /// The active grid, in cells: where a style is allowed to draw. A style never
  /// recomputes an offset from the spectrum, which is what keeps the object
  /// still while it animates.
  int columns = 0;
  int rows = 0;
  /// Beat envelope, 0..1: 1.0 on the onset, decaying after it.
  float beat = 0.0F;
  /// Seconds since the previous update, already clamped by the caller.
  float dt = 1.0F / 60.0F;
  /// True while audio is actually arriving. Silence is a state a style must
  /// handle: it stops spawning and lets what it holds fade out.
  bool live = false;
  const Theme &theme;
  const VisualizerPalette &palette;
};

/// What a style is currently holding. Diagnostics for tests and for the
/// scripted state line; a style must keep all three bounded.
struct VisualizerStats {
  /// Cells the style could draw in (0 when it has no state).
  int capacity = 0;
  /// Cells it is currently holding non-zero state for.
  int retained = 0;
  /// Cells the last frame actually drew.
  int drawn = 0;
};

/// One visualizer style. Three of them exist; the interface is deliberately
/// this small so a fourth is a new file, not a new branch in the UI.
class VisualizerRenderer {
public:
  virtual ~VisualizerRenderer() = default;
  /// The registry id, e.g. "classic-bars".
  virtual std::string_view id() const = 0;
  /// Drops EVERY byte of runtime state. Called when the style is selected and
  /// when the geometry changes, so state can never leak between styles.
  virtual void reset() = 0;
  /// Advances the style's own state by one frame.
  virtual void update(const VisualizerFrame &frame) = 0;
  /// Draws the current state. Must stay inside frame.columns x frame.rows.
  /// Non-const: a style caches the cell grid it builds, which is what keeps a
  /// frame's allocations bounded.
  virtual ftxui::Element render(const VisualizerFrame &frame) = 0;
  virtual VisualizerStats stats() const { return {}; }
};

/// One entry of the style registry.
struct VisualizerStyle {
  std::string_view id;
  std::string_view label; ///< what the Appearance select shows
  std::unique_ptr<VisualizerRenderer> (*make)();
};

/// Exactly the three supported styles, in display order.
const std::vector<VisualizerStyle> &visualizerStyles();

/// The canonical style id: "classic-bars".
std::string_view defaultVisualizerStyleId();

/// True when `id` names a registered style. Legacy identifiers ("city",
/// "cyber-city", "skyline") are NOT styles.
bool isVisualizerStyle(std::string_view id);

/// A stored or configured value resolved to a style that exists. Anything a
/// registry lookup cannot answer -- a legacy city id, a typo, an empty string
/// -- becomes the canonical default, so a renderer can never fail to
/// initialize.
std::string_view normalizeVisualizerStyleId(std::string_view id);

/// The renderer factory: the normalized style, always a valid renderer.
std::unique_ptr<VisualizerRenderer> makeVisualizerRenderer(std::string_view id);

/// The display label for a style id (the label of the normalized style).
std::string_view visualizerStyleLabel(std::string_view id);

/// A rectangular grid of cells that a style fills and the renderer turns into
/// rows. One allocation per resize, and consecutive cells that share a glyph and
/// an intensity bucket become ONE coloured run, so a frame costs runs rather
/// than cells.
class CellGrid {
public:
  void resize(int columns, int rows);
  void clear();
  int columns() const { return columns_; }
  int rows() const { return rows_; }
  /// Draws `glyph` (must be one cell wide) at (x, y). Out-of-range writes are
  /// dropped rather than clamped: a style that overflows is a bug, and it must
  /// not corrupt the neighbouring cell.
  void put(int x, int y, std::string_view glyph, float intensity);
  /// True when a cell holds something.
  bool filled(int x, int y) const;
  float intensityAt(int x, int y) const;
  int filledCells() const { return filled_; }
  ftxui::Element toElement(const VisualizerFrame &frame) const;

private:
  int index(int x, int y) const { return y * columns_ + x; }
  int columns_ = 0;
  int rows_ = 0;
  int filled_ = 0;
  std::vector<std::string_view> glyph_;
  std::vector<float> intensity_;
};

} // namespace termusic::ui
