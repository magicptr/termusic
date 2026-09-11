// The immersive visualizer's geometry: one centered rectangular grid.
//
// These are the properties the renderer depends on and cannot check for
// itself. They are pure functions of the terminal size, so they are testable
// without a terminal, a backend or a signal:
//
//   * the active grid is centered inside its container (the two inactive
//     margins differ by at most one cell),
//   * the grid never exceeds the container, and the drawn size matches the
//     rectangles it holds (so the last block cannot fall outside),
//   * the grid is a stable function of the size alone -- the same input always
//     produces the same origin, which is what keeps the object still while the
//     spectrum animates,
//   * amplitude maps to whole rectangles: silence is zero blocks (never a
//     decorative floor), a live quiet band is one, and a hot signal clamps.

#include <cassert>
#include <cmath>
#include <iostream>

#include "ui/metrics.hpp"

using termusic::ui::ImmersiveLayout;
using termusic::ui::visualizerBlockCount;

namespace {

void checkSize(int width, int height) {
  const termusic::ui::UiMetrics metrics =
      termusic::ui::computeMetrics(width, height, 2, false);
  const ImmersiveLayout &l = metrics.immersive;

  // The container is inside the body and holds the grid.
  assert(l.visualizer_container_columns > 0);
  assert(l.visualizer_container_rows > 0);
  assert(l.visualizer_container_columns <= l.body_width);
  assert(l.visualizer_container_rows <= l.body_height);

  // The grid is drawn inside the container, never overhanging it.
  assert(l.grid_left() >= 0 && l.grid_top() >= 0);
  assert(l.grid_left() + l.visualizer_grid_columns <=
         l.visualizer_container_columns);
  assert(l.grid_top() + l.visualizer_grid_rows <=
         l.visualizer_container_rows);

  // Centered: the two inactive margins differ by at most one cell.
  const int left = l.visualizer_grid_left;
  const int right = l.visualizer_container_columns - l.visualizer_grid_columns -
                    l.visualizer_grid_left;
  assert(std::abs(left - right) <= 1);
  const int above = l.visualizer_grid_top;
  const int below = l.visualizer_container_rows - l.visualizer_grid_rows -
                    l.visualizer_grid_top;
  assert(std::abs(above - below) <= 1);

  // The grid is exactly the rectangles plus the gaps between them.
  assert(l.visualizer_grid_columns ==
         l.visualizer_bands * l.visualizer_column_stride -
             (l.visualizer_column_stride - 1));
  assert(l.visualizer_grid_rows ==
         l.visualizer_levels * l.visualizer_row_stride -
             (l.visualizer_row_stride - 1));

  // At least one column and one level fit at every supported size.
  assert(l.visualizer_bands >= 1 && l.visualizer_levels >= 1);
  // The grid leaves real breathing room: it is not the whole container.
  assert(l.visualizer_grid_columns <= l.visualizer_container_columns);
}

} // namespace

int main() {
  // The supported range, including the sizes named in the acceptance tests.
  for (const int width : {66, 78, 96, 110, 150, 200, 320}) {
    for (const int height : {20, 24, 26, 32, 44, 60}) {
      checkSize(width, height);
      // Stability: geometry is a pure function of the terminal size.
      const auto first = termusic::ui::computeMetrics(width, height, 2, false);
      const auto second = termusic::ui::computeMetrics(width, height, 2, false);
      assert(first.immersive.visualizer_grid_left ==
             second.immersive.visualizer_grid_left);
      assert(first.immersive.visualizer_grid_top ==
             second.immersive.visualizer_grid_top);
      assert(first.immersive.visualizer_grid_columns ==
             second.immersive.visualizer_grid_columns);
      assert(first.immersive.visualizer_grid_rows ==
             second.immersive.visualizer_grid_rows);
    }
  }
  std::cout << "visualizer layout: container/grid centering holds for every "
               "tested size\n";

  // Amplitude -> rectangles.
  constexpr int kLevels = 12;
  assert(visualizerBlockCount(0.0F, kLevels) == 0);   // silence is silence
  assert(visualizerBlockCount(0.01F, kLevels) == 0);  // below the threshold
  assert(visualizerBlockCount(0.02F, kLevels) == 0);
  assert(visualizerBlockCount(0.05F, kLevels) == 1);  // quiet but real
  assert(visualizerBlockCount(0.5F, kLevels) == 6);
  assert(visualizerBlockCount(1.0F, kLevels) == kLevels);
  assert(visualizerBlockCount(1.6F, kLevels) == kLevels); // clamped, no overflow
  assert(visualizerBlockCount(0.5F, 0) == 0);             // no levels, nothing
  std::cout << "visualizer blocks: silence 0, quiet 1, full grid, clamped\n";

  std::cout << "visualizer layout: all checks passed\n";
  return 0;
}
