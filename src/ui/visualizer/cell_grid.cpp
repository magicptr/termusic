#include "ui/visualizer/renderer.hpp"

#include <algorithm>
#include <cmath>

#include "util/text.hpp"

namespace termusic::ui {
namespace {

using namespace ftxui;

/// Intensity quantisation for run grouping. 24 steps is finer than any terminal
/// palette can show, and it is what keeps a frame at a handful of runs per row
/// instead of one element per cell.
constexpr int kIntensityBuckets = 24;

int bucketOf(float intensity) {
  const int bucket = static_cast<int>(
      std::lround(std::clamp(intensity, 0.0F, 1.0F) *
                  static_cast<float>(kIntensityBuckets)));
  return std::clamp(bucket, 0, kIntensityBuckets);
}

} // namespace

void CellGrid::resize(int columns, int rows) {
  columns_ = std::max(0, columns);
  rows_ = std::max(0, rows);
  glyph_.assign(static_cast<std::size_t>(columns_) *
                    static_cast<std::size_t>(rows_),
                std::string_view{});
  intensity_.assign(static_cast<std::size_t>(columns_) *
                        static_cast<std::size_t>(rows_),
                    0.0F);
  filled_ = 0;
}

void CellGrid::clear() {
  std::fill(glyph_.begin(), glyph_.end(), std::string_view{});
  std::fill(intensity_.begin(), intensity_.end(), 0.0F);
  filled_ = 0;
}

void CellGrid::put(int x, int y, std::string_view glyph, float intensity) {
  if (x < 0 || y < 0 || x >= columns_ || y >= rows_)
    return;
  const auto at = static_cast<std::size_t>(index(x, y));
  if (glyph_[at].empty())
    ++filled_;
  glyph_[at] = glyph;
  intensity_[at] = std::clamp(intensity, 0.0F, 1.0F);
}

bool CellGrid::filled(int x, int y) const {
  if (x < 0 || y < 0 || x >= columns_ || y >= rows_)
    return false;
  return !glyph_[static_cast<std::size_t>(index(x, y))].empty();
}

float CellGrid::intensityAt(int x, int y) const {
  if (x < 0 || y < 0 || x >= columns_ || y >= rows_)
    return 0.0F;
  return intensity_[static_cast<std::size_t>(index(x, y))];
}

Element CellGrid::toElement(const VisualizerFrame &frame) const {
  Elements rows;
  rows.reserve(static_cast<std::size_t>(std::max(0, rows_)));
  for (int y = 0; y < rows_; ++y) {
    Elements runs;
    int x = 0;
    while (x < columns_) {
      const auto at = static_cast<std::size_t>(index(x, y));
      if (glyph_[at].empty()) {
        // Blank cells are collapsed into one run too: a sparse style (particles)
        // must not emit one element per empty column.
        int end = x;
        while (end < columns_ &&
               glyph_[static_cast<std::size_t>(index(end, y))].empty())
          ++end;
        runs.push_back(text(util::repeat(end - x, " ")));
        x = end;
        continue;
      }
      const std::string_view glyph = glyph_[at];
      const int bucket = bucketOf(intensity_[at]);
      int end = x + 1;
      while (end < columns_) {
        const auto next = static_cast<std::size_t>(index(end, y));
        if (glyph_[next] != glyph || bucketOf(intensity_[next]) != bucket)
          break;
        ++end;
      }
      runs.push_back(
          text(util::repeat(end - x, std::string(glyph))) |
          color(frame.palette.ramp(static_cast<float>(bucket) /
                                       static_cast<float>(kIntensityBuckets),
                                   frame.theme)));
      x = end;
    }
    rows.push_back(hbox(std::move(runs)));
  }
  return vbox(std::move(rows));
}

} // namespace termusic::ui
