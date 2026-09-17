#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "ui/visualizer/disc.hpp"

namespace {

using termusic::PlaybackState;
using termusic::ui::DiscFrame;
using termusic::ui::DiscRenderer;
using termusic::ui::kDiscCellAspect;

DiscFrame frame(int columns, int rows, PlaybackState playback,
                double dt = 1.0 / 15.0) {
  return {.columns = columns,
          .rows = rows,
          .dt = dt,
          .playback = playback,
          .theme = {}};
}

void draw(DiscRenderer &disc, const DiscFrame &value, int steps = 1) {
  for (int step = 0; step < steps; ++step)
    disc.update(value);
  (void)disc.render(value);
}

} // namespace

int main() {
  DiscRenderer disc;

  // Reference-like terminal geometry: the disc is centred, physically round,
  // about 68% of the width and 80% of the visual region.
  const auto reference = frame(58, 22, PlaybackState::Stopped);
  draw(disc, reference);
  auto stats = disc.stats();
  assert(stats.geometry_fits);
  assert(stats.center_x == 29 && stats.center_y == 10);
  assert(stats.tonearm_position == 1.0);
  assert(stats.tonearm_bend > 0.02);
  const double reference_arm_bend = stats.tonearm_bend;
  const double width_share =
      static_cast<double>(stats.right - stats.left + 1) / 58.0;
  const double height_share =
      static_cast<double>(stats.bottom - stats.top + 1) / 22.0;
  assert(width_share >= 0.64 && width_share <= 0.74);
  assert(height_share >= 0.76 && height_share <= 0.88);
  const double physical_width =
      static_cast<double>(stats.right - stats.left) * kDiscCellAspect;
  const double physical_height = static_cast<double>(stats.bottom - stats.top);
  assert(std::fabs(physical_width - physical_height) <= 1.1);

  ftxui::Screen screen(58, 22);
  ftxui::Render(screen, disc.render(reference));
  assert(screen.CellAt(stats.center_x, stats.center_y).foreground_color ==
         ftxui::Color::RGB(0, 0, 0));
  assert(screen.CellAt(stats.center_x - 3, stats.center_y).foreground_color ==
         reference.theme.accent_primary);

  // Only the label follows the live theme. Re-rendering the same geometry with
  // another accent changes its lit face immediately, without rebuilding the
  // renderer or recolouring the spindle.
  DiscFrame themed = reference;
  themed.theme.accent_primary = ftxui::Color::RGB(32, 146, 208);
  ftxui::Screen themed_screen(58, 22);
  ftxui::Render(themed_screen, disc.render(themed));
  assert(themed_screen.CellAt(stats.center_x - 3, stats.center_y)
             .foreground_color == themed.theme.accent_primary);
  assert(themed_screen.CellAt(stats.center_x, stats.center_y).foreground_color ==
         ftxui::Color::RGB(0, 0, 0));

  // Every point well inside the record is filled; there are no holes between
  // groove bands. The pivoted arm uses only fixed neutral metal colours.
  for (int y = 0; y < 22; ++y) {
    for (int x = 0; x < 58; ++x) {
      const double dx = (x - stats.center_x) * kDiscCellAspect;
      const double dy = y - stats.center_y;
      if (std::hypot(dx, dy) <= stats.radius_rows * 0.92)
        assert(screen.CellAt(x, y).character != " ");
    }
  }
  bool found_arm_tube = false;
  bool found_arm_pin = false;
  bool found_arm_shadow = false;
  for (int y = 0; y < 22; ++y) {
    for (int x = 0; x < 58; ++x) {
      const auto color = screen.CellAt(x, y).foreground_color;
      found_arm_tube |= color == ftxui::Color::RGB(174, 172, 181);
      found_arm_pin |= color == ftxui::Color::RGB(160, 159, 166);
      found_arm_shadow |= color == ftxui::Color::RGB(40, 39, 45);
    }
  }
  assert(found_arm_tube);
  assert(found_arm_pin);
  assert(found_arm_shadow);

  // Playback rotates the groove and brings the arm onto the record. Pausing or
  // stopping freezes the groove while parking the arm outside.
  const double stopped_phase = stats.rotation_phase;
  draw(disc, frame(58, 22, PlaybackState::Playing), 10);
  const double playing_phase = disc.stats().rotation_phase;
  assert(playing_phase != stopped_phase);
  assert(disc.stats().tonearm_position == 0.0);
  assert(disc.stats().tonearm_head_radius >= 0.80 &&
         disc.stats().tonearm_head_radius <= 0.86);
  assert(std::fabs(disc.stats().tonearm_bend - reference_arm_bend) < 1e-9);
  draw(disc, frame(58, 22, PlaybackState::Paused));
  assert(disc.stats().rotation_phase == playing_phase);
  assert(disc.stats().tonearm_position > 0.0 &&
         disc.stats().tonearm_position < 1.0);
  assert(std::fabs(disc.stats().tonearm_bend - reference_arm_bend) < 1e-9);
  assert(disc.animationActive());
  draw(disc, frame(58, 22, PlaybackState::Paused), 10);
  assert(disc.stats().rotation_phase == playing_phase);
  assert(disc.stats().tonearm_position == 1.0);
  assert(!disc.animationActive());
  draw(disc, frame(58, 22, PlaybackState::Stopped));
  assert(disc.stats().tonearm_position == 1.0);
  assert(!disc.animationActive());

  for (const auto [columns, rows] : {
           std::pair{50, 18}, std::pair{66, 24}, std::pair{110, 28},
           std::pair{150, 38}}) {
    const auto resized = frame(columns, rows, PlaybackState::Stopped);
    draw(disc, resized);
    assert(disc.stats().geometry_fits);
  }

  std::cout << "disc: reference geometry, sampled palette, pivoted arm and "
               "responsive bounds passed\n";
}
