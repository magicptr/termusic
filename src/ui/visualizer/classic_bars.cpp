// Classic Bars: rectangular bars on a common baseline.
//
// The simplest, most readable style, and the canonical fallback: one bar per
// frequency slot, height driven by the spectrum, a row of small rectangular
// cells as the baseline, and a peak marker that decays on its own. No braille,
// no particles, no scatter.

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

#include "ui/visualizer/renderer.hpp"

namespace termusic::ui {
namespace {

using namespace ftxui;

/// The bar body: a full block, i.e. a solid rectangle one cell wide.
constexpr std::string_view kBarCell = "\u2588";
/// The baseline cell and the peak marker: the SAME small rectangle, so the
/// style has one secondary glyph rather than a vocabulary.
constexpr std::string_view kSmallCell = "\u25aa";

/// How fast a bar follows the spectrum. Rising is quick (a hit is visible
/// immediately), falling is slower so the eye can follow it.
constexpr float kRiseTau = 0.045F;
constexpr float kFallTau = 0.160F;
/// Peak marker: it snaps up with the bar and then slides down at this rate
/// (rows per second), so it reads as a held maximum.
constexpr float kPeakFallRows = 6.0F;
constexpr float kPeakHoldTau = 0.35F;

class ClassicBarsRenderer final : public VisualizerRenderer {
public:
  std::string_view id() const override { return "classic-bars"; }

  void reset() override {
    heights_.clear();
    peak_rows_.clear();
    peak_hold_.clear();
    columns_ = 0;
    rows_ = 0;
    drawn_ = 0;
  }

  void update(const VisualizerFrame &frame) override {
    if (frame.columns != columns_ || frame.rows != rows_) {
      // A resize is a fresh geometry, not a reason to keep stale bar heights
      // that belong to other columns.
      columns_ = frame.columns;
      rows_ = frame.rows;
      heights_.assign(static_cast<std::size_t>(std::max(0, columns_)), 0.0F);
      peak_rows_.assign(heights_.size(), 0.0F);
      peak_hold_.assign(heights_.size(), 0.0F);
    }
    const float step = std::clamp(frame.dt, 0.0F, 0.25F);
    const float rise = 1.0F - std::exp(-step / kRiseTau);
    const float fall = 1.0F - std::exp(-step / kFallTau);
    for (int column = 0; column < columns_; ++column) {
      const auto at = static_cast<std::size_t>(column);
      const float value = frame.live ? bandAt(frame, column) : 0.0F;
      const float target = static_cast<float>(rows_) * value;
      heights_[at] += (target - heights_[at]) *
                      (target > heights_[at] ? rise : fall);
      heights_[at] = std::clamp(heights_[at], 0.0F,
                                static_cast<float>(std::max(1, rows_)));
      if (heights_[at] >= peak_rows_[at]) {
        peak_rows_[at] = heights_[at];
        peak_hold_[at] = kPeakHoldTau;
      } else {
        peak_hold_[at] = std::max(0.0F, peak_hold_[at] - step);
        if (peak_hold_[at] <= 0.0F)
          peak_rows_[at] =
              std::max(heights_[at], peak_rows_[at] - kPeakFallRows * step);
      }
    }
  }

  Element render(const VisualizerFrame &frame) override {
    grid_.resize(frame.columns, frame.rows);
    grid_.clear();
    drawn_ = 0;
    if (columns_ <= 0 || rows_ <= 0)
      return grid_.toElement(frame);
    for (int column = 0; column < columns_ && column < frame.columns;
         ++column) {
      const auto at = static_cast<std::size_t>(column);
      const int height = std::clamp(static_cast<int>(std::lround(heights_[at])),
                                    0, rows_);
      // The baseline: one small cell per slot, always present, so the bars
      // stand on a common, visible floor instead of on nothing.
      grid_.put(column, rows_ - 1, kSmallCell,
                0.22F + 0.10F * static_cast<float>(frame.beat));
      ++drawn_;
      // The body, drawn upward from the baseline.
      for (int level = 1; level < height; ++level) {
        const int y = rows_ - 1 - level;
        const float intensity =
            std::clamp(heights_[at] / static_cast<float>(std::max(1, rows_)),
                       0.12F, 1.0F);
        grid_.put(column, y, kBarCell, intensity);
        ++drawn_;
      }
      // The peak marker, one row above the bar when it is holding a maximum.
      const int peak = std::clamp(
          static_cast<int>(std::lround(peak_rows_[at])), 0, rows_ - 1);
      if (peak > height) {
        const int y = rows_ - 1 - peak;
        if (!grid_.filled(column, y)) {
          grid_.put(column, y, kSmallCell,
                    std::clamp(static_cast<float>(peak) /
                                   static_cast<float>(std::max(1, rows_)),
                               0.3F, 1.0F));
          ++drawn_;
        }
      }
    }
    return grid_.toElement(frame);
  }

  VisualizerStats stats() const override {
    VisualizerStats stats;
    stats.capacity = columns_;
    for (int column = 0; column < columns_; ++column) {
      if (heights_[static_cast<std::size_t>(column)] > 0.05F)
        ++stats.retained;
    }
    stats.drawn = drawn_;
    return stats;
  }

private:
  /// The band under one bar slot. Bars are spread across the WHOLE spectrum
  /// (bass on the left), which is what makes the style readable at any width.
  static float bandAt(const VisualizerFrame &frame, int column) {
    const int bands = static_cast<int>(frame.bands.size());
    if (bands <= 0)
      return 0.0F;
    if (frame.columns <= 1 || bands == 1)
      return std::clamp(frame.bands.front(), 0.0F, 1.0F);
    const double position =
        static_cast<double>(column) * static_cast<double>(bands - 1) /
        static_cast<double>(frame.columns - 1);
    const auto lower = static_cast<std::size_t>(position);
    const auto upper = std::min(lower + 1U, static_cast<std::size_t>(bands - 1));
    const float value = std::lerp(
        frame.bands[lower], frame.bands[upper],
        static_cast<float>(position - static_cast<double>(lower)));
    return std::clamp(value, 0.0F, 1.0F);
  }

  int columns_ = 0;
  int rows_ = 0;
  int drawn_ = 0;
  std::vector<float> heights_;
  std::vector<float> peak_rows_;
  std::vector<float> peak_hold_;
  CellGrid grid_;
};

} // namespace

std::unique_ptr<VisualizerRenderer> makeClassicBarsRenderer() {
  return std::make_unique<ClassicBarsRenderer>();
}

} // namespace termusic::ui
