// Spectrum: the ONE visualizer.
//
//   Analyzer -> bands -> bar heights -> light smoothing -> what is drawn
//
// What it draws:
//
//   * THIN INDEPENDENT BARS: one cell of bar, one cell of air, one cell of bar
//     again. The Spectrum is a row of bars, never a solid band of colour.
//   * SUB-CELL RESOLUTION: the partial block glyphs (U+2581..U+2588) give eight
//     steps per row, so a two-row bar still shows its shape.
//   * A VERTICAL GRADIENT keyed on the ABSOLUTE height above the baseline, so
//     the ramp belongs to the screen and not to a bar: a short bar can only
//     show the low end of it.
//   * A DOT BASELINE: one dot under every bar, on the row the bars stand on.
//     The dots belong to the VISUALIZER, not to the audio -- they are drawn
//     whenever the Spectrum is on screen, so a stopped player shows the row of
//     dots and no bars at all.
//   * A CENTRED, WIDE OBJECT: the bars use most of the drawable width, with the
//     same margin on both sides, and sit in the lower part of the page.
//
// The Spectrum is the bars and the dot baseline, and nothing else.

#include "ui/visualizer/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "util/text.hpp"

namespace termusic::ui {
namespace {

using namespace ftxui;

/// How fast a bar follows the music. Rising is quick (a hit is visible
/// immediately), falling is slower so the eye can follow it. The analyzer has
/// already smoothed the signal; this only keeps the drawn bars from flickering
/// between rows.
constexpr float kRiseTau = 0.045F;
constexpr float kFallTau = 0.110F;

/// Sub-rows per cell: the partial block glyphs give eight steps per row.
constexpr int kSubRows = 8;

/// The eight partial/full block glyphs, indexed by eighths: 1/8 .. 8/8.
constexpr std::string_view kBlockGlyphs[kSubRows] = {
    "\u2581", "\u2582", "\u2583", "\u2584",
    "\u2585", "\u2586", "\u2587", "\u2588",
};

/// The dot that marks the baseline under every bar. One cell wide, and small
/// enough to read as a dot rather than as a rule.
constexpr std::string_view kDotGlyph = "\u00b7";

/// How much of the drawable width the bars and their air take. The object is
/// centred, so the leftover is split between the two margins. The share rises
/// as the window narrows, where every cell counts.
constexpr float kWidthLarge = 0.90F;  // >= 100 columns
constexpr float kWidthMedium = 0.92F; // >= 60
constexpr float kWidthSmall = 0.94F;  // narrower than that

/// One cell of bar, one cell of air: the pattern the whole layout is built on.
constexpr int kBarCellWidth = 1;
constexpr int kGapCellWidth = 1;

/// How much of the drawable height above the dot row the bars may use. The rest
/// is air, and it is all ABOVE them: the Spectrum sits in the lower part of the
/// page, under the title and clear of the Player Bar.
constexpr int kBarHeightPercent = 62;

/// The centre emphasis of the bar axis: how much more of the drawn width the
/// middle of the spectrum takes than its two ends. 1.0 is the plain linear
/// spread; above 1 the middle bands are given more bars and the extremes are
/// compressed, so the shape gathers in the centre instead of pressing its
/// loudest bands against the frame.
constexpr double kCentreEmphasis = 1.30;

/// The falloff toward the ends, as a fraction of the drawn height. The curve is
/// deliberately FLAT across the middle (the exponent) and only bites over the
/// outer third: the centre keeps its full height, the ends settle to about two
/// thirds. It is applied to the drawn bar HEIGHT only, never to the audio.
constexpr float kEdgeFalloff = 0.35F;
constexpr float kEdgeFalloffCurve = 2.5F;

/// Above this height a bar is compressed smoothly instead of flattening against
/// the top of its area: a band at full scale ends up just under the ceiling, so
/// a loud passage cannot look "cut off". The knee starts late on purpose: below
/// it the drawn height IS the band value, so the whole loud range keeps the
/// differences the music puts there.
constexpr float kSoftKneeStart = 0.86F;

/// A bar whose height falls under this is DROPPED rather than drawn as a
/// sliver: after the decay the bars must be COMPLETELY gone, or a stopped
/// player would keep a row of stubs above the dots for ever.
constexpr float kBarVisibilityFloor = 0.004F;

/// A light smoothing across NEIGHBOURING bars, applied to the height that is
/// actually drawn: h' = (left + 7*centre + right) / 9. It takes the staircase
/// and the one-bar spikes out of the contour while leaving the temporal
/// response alone -- a peak that is several bars wide, which is what music
/// produces, keeps its shape. Deliberately weaker than a plain 1-2-1 average:
/// the step between neighbouring bands is part of what the eye reads as
/// movement, so it may be softened, not flattened.
constexpr float kNeighbourSmoothing = 1.0F / 3.0F;

float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }

/// How much of the drawable width the object uses.
float widthRatio(int columns) {
  if (columns >= 100)
    return kWidthLarge;
  if (columns >= 60)
    return kWidthMedium;
  return kWidthSmall;
}

/// How many bars fit: each bar is one cell with one cell of air after it, so a
/// pair is two cells wide and the last bar needs no trailing air.
int barCount(int columns) {
  if (columns <= 0)
    return 0;
  const int wanted = static_cast<int>(
      std::lround(static_cast<float>(columns) * widthRatio(columns)));
  const int pairs = std::max(1, wanted / (kBarCellWidth + kGapCellWidth));
  return std::clamp(pairs, 1, std::max(1, (columns + 1) / 2));
}

/// The cell the first bar starts on: the object (bars and the air between them,
/// with no trailing air) is centred in the drawable width.
int barLeft(int columns, int bars) {
  if (bars <= 0)
    return 0;
  const int pixel_width = bars * (kBarCellWidth + kGapCellWidth) - kGapCellWidth;
  return std::max(0, (columns - pixel_width) / 2);
}

/// The height weight of a bar inside the object: 1.0 across the middle,
/// kEdgeFalloff less at the very ends, and smooth in between.
float edgeWeight(int position, int bars) {
  if (bars <= 2)
    return 1.0F;
  const float half = static_cast<float>(bars - 1) * 0.5F;
  const float normalized =
      std::fabs(static_cast<float>(position) - half) / half; // 0 centre, 1 end
  return 1.0F - kEdgeFalloff * std::pow(normalized, kEdgeFalloffCurve);
}

/// The height a bar is DRAWN with: the end falloff above, and a soft knee under
/// the ceiling so a saturated band approaches the top smoothly instead of
/// ending in a flat, clipped plateau.
float drawnHeight(float height) {
  const float value = clamp01(height);
  if (value <= kSoftKneeStart)
    return value;
  const float span = 1.0F - kSoftKneeStart;
  const float over = (value - kSoftKneeStart) / span;
  // Asymptotic: full scale lands just under the ceiling, never on it.
  return kSoftKneeStart + span * (1.0F - std::exp(-over * 1.8F)) * 0.93F;
}

/// The band under one bar. The bars still span the WHOLE spectrum, bass on the
/// left -- but the axis is warped so the middle of the spectrum takes more of
/// the drawn width and the two ends are compressed. The mapping stays monotonic
/// and covers every band: nothing is dropped, it is redistributed.
float bandAt(const VisualizerFrame &frame, int bar, int bars) {
  const int bands = static_cast<int>(frame.bands.size());
  if (bands <= 0)
    return 0.0F;
  if (bars <= 1 || bands == 1)
    return clamp01(frame.bands.front());
  const double x = static_cast<double>(std::clamp(bar, 0, bars - 1)) /
                   static_cast<double>(bars - 1);
  const double centred = (x - 0.5) * 2.0; // -1..1
  const double warped =
      0.5 + 0.5 * std::copysign(std::pow(std::fabs(centred), kCentreEmphasis),
                                centred);
  const double position = warped * static_cast<double>(bands - 1);
  const auto lower = static_cast<std::size_t>(position);
  const auto upper = std::min(lower + 1U, static_cast<std::size_t>(bands - 1));
  return clamp01(
      std::lerp(frame.bands[lower], frame.bands[upper],
                static_cast<float>(position - static_cast<double>(lower))));
}

/// One cell of the frame being built: a glyph and the colour it is drawn in.
struct Cell {
  std::string_view glyph; ///< empty = nothing here
  Color color = Color::Default;
};

class SpectrumRenderer final : public VisualizerRenderer {
public:
  std::string_view id() const override { return kVisualizerId; }

  void reset() override {
    heights_.clear();
    smoothed_.clear();
    cells_.clear();
    frame_cells_.clear();
    bars_ = 0;
    rows_ = 0;
    columns_ = 0;
    stats_ = {};
  }

  void update(const VisualizerFrame &frame) override {
    const int bars = barCount(frame.columns);
    if (bars != bars_ || frame.rows != rows_) {
      // A resize is a fresh geometry: heights that belonged to another bar
      // count must not be stretched onto the new ones.
      bars_ = bars;
      rows_ = frame.rows;
      heights_.assign(static_cast<std::size_t>(std::max(0, bars_)), 0.0F);
      smoothed_.assign(heights_.size(), 0.0F);
    }
    active_left_ = barLeft(frame.columns, bars_);

    const float step = std::clamp(frame.dt, 0.0F, 0.25F);
    const float rise = 1.0F - std::exp(-step / kRiseTau);
    const float fall = 1.0F - std::exp(-step / kFallTau);
    for (int bar = 0; bar < bars_; ++bar) {
      const auto at = static_cast<std::size_t>(bar);
      const float target =
          frame.live ? drawnHeight(bandAt(frame, bar, bars_) *
                                   edgeWeight(bar, bars_))
                     : 0.0F;
      heights_[at] +=
          (target - heights_[at]) * (target > heights_[at] ? rise : fall);
      heights_[at] = clamp01(heights_[at]);
      if (heights_[at] < kBarVisibilityFloor)
        heights_[at] = 0.0F;
    }
    smoothBars();
    for (int bar = 0; bar < bars_; ++bar) {
      const auto at = static_cast<std::size_t>(bar);
      if (heights_[at] < kBarVisibilityFloor)
        heights_[at] = 0.0F;
    }
  }

  Element render(const VisualizerFrame &frame) override {
    const int columns = std::max(0, frame.columns);
    const int rows = std::max(0, frame.rows);
    if (columns != columns_ || rows != rows_) {
      columns_ = columns;
      rows_ = rows;
      cells_.assign(static_cast<std::size_t>(columns_), Cell{});
      frame_cells_.assign(static_cast<std::size_t>(columns_) *
                              static_cast<std::size_t>(rows_),
                          Cell{});
    } else {
      std::fill(frame_cells_.begin(), frame_cells_.end(), Cell{});
    }

    const int bars = std::min(bars_, (columns + 1) / 2);
    const auto at = [&](int x, int y) -> Cell & {
      return frame_cells_[static_cast<std::size_t>(y) *
                              static_cast<std::size_t>(columns_) +
                          static_cast<std::size_t>(x)];
    };

    // The DOT row is the bottom row of the drawable area: the bars stand on the
    // row above it and grow upward from there.
    const int dot_row = rows - 1;
    const int available = std::max(0, dot_row);
    const int bar_rows =
        std::clamp((available * kBarHeightPercent) / 100, 0, available);
    const int lowest_bar_row = dot_row - 1;

    int drawn = 0;
    // The dot baseline: ONE dot under every bar, on the bars' own cells, and
    // drawn whatever the audio is doing -- this is the row that stays when the
    // music stops or when no signal arrives at all.
    if (dot_row >= 0) {
      const Color dot_color = frame.palette.ramp(0.0F, frame.theme);
      for (int bar = 0; bar < bars; ++bar) {
        const int x = active_left_ + bar * (kBarCellWidth + kGapCellWidth);
        if (x < 0 || x >= columns_)
          continue;
        at(x, dot_row) = Cell{kDotGlyph, dot_color};
        ++drawn;
      }
    }
    // The bars, in the rows above the dots. A bar with no height draws nothing:
    // that is what "the bars disappear" means.
    if (bar_rows > 0) {
      for (int bar = 0; bar < bars; ++bar) {
        const float height = heights_[static_cast<std::size_t>(bar)];
        const int sub_rows = static_cast<int>(
            std::lround(height * static_cast<float>(bar_rows) *
                        static_cast<float>(kSubRows)));
        if (sub_rows <= 0)
          continue;
        const int x = active_left_ + bar * (kBarCellWidth + kGapCellWidth);
        if (x < 0 || x >= columns_)
          continue;
        for (int level = 0; level < bar_rows; ++level) {
          const int filled =
              std::clamp(sub_rows - level * kSubRows, 0, kSubRows);
          if (filled <= 0)
            continue;
          const int y = lowest_bar_row - level;
          if (y < 0 || y >= rows)
            continue;
          // The colour comes from the ROW, never from the bar's own height: a
          // short bar can only show the low end of the ramp.
          const float fraction =
              (static_cast<float>(level) + 0.5F) /
              static_cast<float>(std::max(1, bar_rows));
          at(x, y) = Cell{kBlockGlyphs[filled - 1],
                          spectrumColor(fraction, frame.palette, frame.theme)};
          ++drawn;
        }
      }
    }

    // One row at a time: the row slice is copied into `cells_`, where
    // consecutive cells that agree on glyph and colour become one run.
    Elements out;
    out.reserve(static_cast<std::size_t>(rows_));
    for (int y = 0; y < rows_; ++y) {
      for (int x = 0; x < columns_; ++x)
        cells_[static_cast<std::size_t>(x)] = at(x, y);
      out.push_back(rowElement(columns_));
    }

    int retained = 0;
    for (int bar = 0; bar < bars; ++bar) {
      if (heights_[static_cast<std::size_t>(bar)] > 0.01F)
        ++retained;
    }
    stats_.capacity = bars;
    stats_.retained = retained;
    stats_.drawn = drawn;
    stats_.baseline = dot_row;
    stats_.main_rows = bar_rows;
    stats_.bars = bars;
    stats_.dots = bars;
    return vbox(std::move(out));
  }

  VisualizerStats stats() const override { return stats_; }

private:
  /// One pass of neighbour smoothing over the bars, in place.
  void smoothBars() {
    if (bars_ < 3)
      return;
    for (int bar = 1; bar + 1 < bars_; ++bar) {
      const auto at = static_cast<std::size_t>(bar);
      const float left = heights_[at - 1U];
      const float centre = heights_[at];
      const float right = heights_[at + 1U];
      const float average = (left + centre + right) / 3.0F;
      smoothed_[at] = centre + (average - centre) * kNeighbourSmoothing;
    }
    for (int bar = 1; bar + 1 < bars_; ++bar) {
      const auto at = static_cast<std::size_t>(bar);
      heights_[at] = smoothed_[at];
    }
  }

  /// One rendered row: consecutive cells that agree on glyph and colour become
  /// ONE element, so a frame costs runs rather than cells.
  Element rowElement(int columns) const {
    Elements runs;
    int x = 0;
    while (x < columns) {
      const Cell &cell = cells_[static_cast<std::size_t>(x)];
      int end = x + 1;
      while (end < columns && cells_[static_cast<std::size_t>(end)].glyph ==
                                  cell.glyph &&
             cells_[static_cast<std::size_t>(end)].color == cell.color)
        ++end;
      const std::string glyph =
          cell.glyph.empty() ? std::string(" ") : std::string(cell.glyph);
      Element run = text(util::repeat(end - x, glyph));
      if (!cell.glyph.empty())
        run = std::move(run) | color(cell.color);
      runs.push_back(std::move(run));
      x = end;
    }
    if (runs.empty())
      runs.push_back(text(""));
    return hbox(std::move(runs));
  }

  int bars_ = 0;
  int rows_ = 0;
  int columns_ = 0;
  int active_left_ = 0;
  /// One normalised height per BAR (0..1). Bounded by the drawable width.
  std::vector<float> heights_;
  /// Scratch for the neighbour smoothing pass, sized with `heights_` so a frame
  /// never allocates.
  std::vector<float> smoothed_;
  /// The cells of the current row, and of the whole frame while it is built.
  /// Both are sized on a resize, so a frame never allocates.
  std::vector<Cell> cells_;
  std::vector<Cell> frame_cells_;
  VisualizerStats stats_;
};

} // namespace

Color spectrumColor(float heightFraction, const VisualizerPalette &palette,
                    const Theme &theme) {
  // The palette maps a height fraction onto a colour, so the ramp follows the
  // active theme -- and the ICE / FIRE / RAINBOW palettes keep working -- while
  // the POSITION on screen, never a bar's own height, decides where on the ramp
  // a cell sits.
  return palette.ramp(clamp01(heightFraction), theme);
}

std::unique_ptr<VisualizerRenderer> makeSpectrumRenderer() {
  return std::make_unique<SpectrumRenderer>();
}

} // namespace termusic::ui
