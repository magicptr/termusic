// Waterfall: a bounded history of rectangular spectrum cells.
//
// The newest frame enters at the BOTTOM and pushes older frames upward, and each
// row fades with age. Silence is not special-cased: a quiet frame writes a
// quiet row, so the display naturally drains as the history scrolls past.

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

#include "ui/visualizer/renderer.hpp"

namespace termusic::ui {
namespace {

using namespace ftxui;

/// The spectrum cell. One small rectangle, the same unit Classic Bars uses for
/// its baseline, so the two styles share a visual vocabulary.
constexpr std::string_view kCell = "\u25aa";

/// Rows of history per second. A terminal cell is not a pixel: scrolling one row
/// per frame (60/s) would blur the spectrum into noise, and one row per second
/// would not move. 24 rows/s reads as a waterfall at every terminal size.
constexpr float kRowsPerSecond = 24.0F;

/// How much of its intensity a row loses per second of age. Older rows are
/// dimmer, so the eye reads the flow direction without an arrow.
constexpr float kFadePerSecond = 0.55F;

/// A row below this intensity is not drawn at all: silence must CLEAR, not
/// leave a floor of dim cells behind.
constexpr float kVisibleFloor = 0.06F;

class WaterfallRenderer final : public VisualizerRenderer {
public:
  std::string_view id() const override { return "waterfall"; }

  void reset() override {
    history_.clear();
    columns_ = 0;
    rows_ = 0;
    head_ = 0;
    accumulator_ = 0.0F;
    drawn_ = 0;
  }

  void update(const VisualizerFrame &frame) override {
    if (frame.columns != columns_ || frame.rows != rows_) {
      columns_ = frame.columns;
      rows_ = frame.rows;
      history_.assign(static_cast<std::size_t>(std::max(0, columns_) *
                                               std::max(0, rows_)),
                      0.0F);
      head_ = 0;
      accumulator_ = 0.0F;
    }
    if (columns_ <= 0 || rows_ <= 0)
      return;
    const float step = std::clamp(frame.dt, 0.0F, 0.25F);
    // Age every row, in seconds, and drop what has faded out.
    const float fade = kFadePerSecond * step;
    for (float &value : history_)
      value = std::max(0.0F, value - fade);

    accumulator_ += step;
    const float interval = 1.0F / kRowsPerSecond;
    // At most a few rows per frame: a stalled or slow frame must not dump a
    // second of history in one go, and must never grow the buffer.
    int pushed = 0;
    while (accumulator_ >= interval && pushed < 4) {
      accumulator_ -= interval;
      ++pushed;
      pushRow(frame);
    }
  }

  Element render(const VisualizerFrame &frame) override {
    grid_.resize(frame.columns, frame.rows);
    grid_.clear();
    drawn_ = 0;
    if (columns_ <= 0 || rows_ <= 0)
      return grid_.toElement(frame);
    // Row 0 of the grid is the OLDEST visible row; the newest sits at the
    // bottom, so the history extends upward as it scrolls.
    for (int row = 0; row < rows_ && row < frame.rows; ++row) {
      const int y = frame.rows - 1 - row;
      const std::size_t base =
          static_cast<std::size_t>((head_ + rows_ - 1 - row) % rows_) *
          static_cast<std::size_t>(columns_);
      for (int column = 0; column < columns_ && column < frame.columns;
           ++column) {
        const float value = history_[base + static_cast<std::size_t>(column)];
        if (value < kVisibleFloor)
          continue;
        grid_.put(column, y, kCell, std::clamp(value, 0.0F, 1.0F));
        ++drawn_;
      }
    }
    return grid_.toElement(frame);
  }

  VisualizerStats stats() const override {
    VisualizerStats stats;
    stats.capacity = columns_ * rows_;
    for (const float value : history_) {
      if (value >= kVisibleFloor)
        ++stats.retained;
    }
    stats.drawn = drawn_;
    return stats;
  }

private:
  /// Writes the current spectrum as the newest row and advances the ring.
  void pushRow(const VisualizerFrame &frame) {
    head_ = (head_ + 1) % rows_;
    const std::size_t base =
        static_cast<std::size_t>(head_) * static_cast<std::size_t>(columns_);
    const int bands = static_cast<int>(frame.bands.size());
    for (int column = 0; column < columns_; ++column) {
      float value = 0.0F;
      if (frame.live && bands > 0) {
        const double position =
            columns_ <= 1
                ? 0.0
                : static_cast<double>(column) *
                      static_cast<double>(bands - 1) /
                      static_cast<double>(columns_ - 1);
        const auto at = static_cast<std::size_t>(position);
        value = std::clamp(frame.bands[at], 0.0F, 1.0F);
      }
      history_[base + static_cast<std::size_t>(column)] = value;
    }
  }

  int columns_ = 0;
  int rows_ = 0;
  int head_ = 0;
  float accumulator_ = 0.0F;
  int drawn_ = 0;
  std::vector<float> history_;
  CellGrid grid_;
};

} // namespace

std::unique_ptr<VisualizerRenderer> makeWaterfallRenderer() {
  return std::make_unique<WaterfallRenderer>();
}

} // namespace termusic::ui
