#include <algorithm>
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
using termusic::ui::Theme;
using termusic::ui::kDiscCellAspect;

DiscFrame frame(int width, int height, double dt, PlaybackState state,
                double progress, const Theme &theme,
                std::string_view track = "track-a") {
  return DiscFrame{.columns = width,
                   .rows = height,
                   .dt = dt,
                   .playback = state,
                   .progress = progress,
                   .track_identity = track,
                   .theme = theme};
}

void step(DiscRenderer &disc, const DiscFrame &value, int count) {
  for (int i = 0; i < count; ++i) {
    disc.update(value);
    (void)disc.render(value);
  }
}

} // namespace

int main() {
  Theme theme;
  DiscRenderer disc;
  constexpr int width = 96;
  constexpr int height = 24;
  constexpr double dt = 1.0 / 15.0;

  auto stopped = frame(width, height, dt, PlaybackState::Stopped, 0.0, theme);
  step(disc, stopped, 2);
  auto stats = disc.stats();
  assert(stats.parked_outside && stats.arm_clear_of_disc &&
         stats.geometry_fits);
  const double physical_width =
      static_cast<double>(stats.right - stats.left) * kDiscCellAspect;
  const double physical_height = static_cast<double>(stats.radius_rows * 2);
  assert(std::fabs(physical_width - physical_height) <= 1.1);

  auto playing = frame(width, height, dt, PlaybackState::Playing, 0.1, theme);
  const double phase_before = stats.rotation_phase;
  const double parked_angle = stats.arm_angle;
  disc.update(playing);
  (void)disc.render(playing);
  assert(std::fabs(disc.stats().arm_angle - parked_angle) <= 0.121 &&
         "one block frame must not make a large angular jump");
  assert(!disc.stats().needle_on_disc &&
         "Stopped -> Playing must begin as a transition, not teleport");
  step(disc, playing, 14);
  stats = disc.stats();
  assert(stats.needle_on_disc && stats.rotation_phase != phase_before);
  assert(stats.contact_radius >= 0.38 && stats.contact_radius <= 0.82);
  assert(disc.animationActive());

  const double outer_angle = stats.arm_angle;
  auto late = frame(width, height, dt, PlaybackState::Playing, 0.95, theme);
  step(disc, late, 90);
  assert(disc.stats().needle_on_disc);
  assert(disc.stats().arm_angle > outer_angle);
  assert(disc.stats().arm_angle - outer_angle < 0.50 &&
         "the stylized arm should use a restrained groove sweep");

  auto paused = frame(width, height, dt, PlaybackState::Paused, 0.95, theme);
  const double paused_phase = disc.stats().rotation_phase;
  disc.update(paused);
  (void)disc.render(paused);
  assert(disc.animationActive() &&
         "Pause must visibly retract instead of teleporting");
  step(disc, paused, 90);
  assert(disc.stats().parked_outside && disc.stats().arm_clear_of_disc);
  assert(std::fabs(disc.stats().rotation_phase - paused_phase) < 1e-12);
  assert(disc.stats().arm_settled);
  assert(!disc.animationActive());

  auto resumed = frame(width, height, dt, PlaybackState::Playing, 0.95, theme);
  disc.update(resumed);
  (void)disc.render(resumed);
  assert(!disc.stats().needle_on_disc &&
         "Resume must lower smoothly from the parked pose");
  step(disc, resumed, 14);
  assert(disc.stats().needle_on_disc);

  auto next = frame(width, height, dt, PlaybackState::Playing, 0.0, theme,
                    "track-b");
  const double old_angle = disc.stats().arm_angle;
  disc.update(next);
  (void)disc.render(next);
  assert(disc.stats().needle_on_disc);
  assert(disc.stats().arm_angle < old_angle);
  assert(disc.stats().arm_angle > 1.0 &&
         "track changes move toward the outer groove, never via parking");

  const double before_stop_angle = disc.stats().arm_angle;
  disc.update(stopped);
  (void)disc.render(stopped);
  assert(std::fabs(disc.stats().arm_angle - before_stop_angle) <= 0.121 &&
         "retraction must obey the same per-frame angular speed limit");
  assert(disc.stats().arm_angle < before_stop_angle);
  assert(disc.stats().arm_angle > 1.0 &&
         "Playing -> Stopped must retract rather than teleport");
  assert(!disc.stats().arm_clear_of_disc &&
         "the arm must visibly travel out instead of disappearing");
  assert(disc.animationActive());
  step(disc, stopped, 90);
  assert(disc.stats().parked_outside && disc.stats().geometry_fits);
  assert(disc.stats().arm_clear_of_disc &&
         "the complete stopped arm must clear the record, not only its stylus");
  assert(disc.stats().arm_settled && !disc.animationActive());

  // The supplied reference owns the Disc palette: changing the application
  // theme must not recolour the vinyl, label or tonearm.
  auto baseline_frame =
      frame(width, height, dt, PlaybackState::Stopped, 0.0, theme);
  ftxui::Screen baseline_screen(width, height);
  ftxui::Render(baseline_screen, disc.render(baseline_frame));
  Theme alternate = theme;
  alternate.accent_primary = ftxui::Color::RGB(20, 210, 90);
  alternate.panel = ftxui::Color::RGB(230, 210, 30);
  alternate.text = ftxui::Color::RGB(30, 220, 230);
  auto themed = frame(width, height, dt, PlaybackState::Stopped, 0.0, alternate);
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, disc.render(themed));
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      assert(screen.CellAt(x, y).foreground_color ==
             baseline_screen.CellAt(x, y).foreground_color);

  bool found_label_colour = false;
  int label_left = width;
  int label_right = 0;
  int label_top = height;
  int label_bottom = 0;
  const auto geometry = disc.stats();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double dx = (x - geometry.center_x) * kDiscCellAspect;
      const double dy = y - geometry.center_y;
      const double radius = std::hypot(dx, dy) /
                            static_cast<double>(geometry.radius_rows);
      if (radius <= 0.90)
        assert(screen.CellAt(x, y).character != " " &&
               "the vinyl interior must be continuously filled");
    }
  }
  const int label_probe = std::max(
      1, static_cast<int>(std::lround(geometry.radius_rows * 0.15 /
                                     kDiscCellAspect)));
  const ftxui::Color label_main =
      screen.CellAt(geometry.center_x - label_probe, geometry.center_y)
          .foreground_color;
  const ftxui::Color label_shadow =
      screen.CellAt(geometry.center_x + label_probe, geometry.center_y)
          .foreground_color;
  for (int y = geometry.center_y - geometry.radius_rows / 3;
       y <= geometry.center_y + geometry.radius_rows / 3; ++y) {
    for (int x = geometry.center_x - geometry.radius_rows;
         x <= geometry.center_x + geometry.radius_rows; ++x) {
      if (x >= 0 && x < width && y >= 0 && y < height &&
          (screen.CellAt(x, y).foreground_color == label_main ||
           screen.CellAt(x, y).foreground_color == label_shadow)) {
        found_label_colour = true;
        label_left = std::min(label_left, x);
        label_right = std::max(label_right, x);
        label_top = std::min(label_top, y);
        label_bottom = std::max(label_bottom, y);
      }
    }
  }
  assert(found_label_colour);
  const double label_physical_width =
      static_cast<double>(label_right - label_left + 1) * kDiscCellAspect;
  const double label_physical_height =
      static_cast<double>(label_bottom - label_top + 1);
  assert(std::fabs(label_physical_width - label_physical_height) <= 1.2 &&
         "the theme label must be physically circular, not a cell square");

  for (const auto [w, h] : {std::pair{66, 12}, std::pair{78, 18},
                            std::pair{110, 28}, std::pair{150, 38}}) {
    auto resized = frame(w, h, dt, PlaybackState::Stopped, 0.0, alternate);
    step(disc, resized, 90);
    assert(disc.stats().geometry_fits);
    assert(disc.stats().parked_outside);
  }

  // Representative real-cell render for a human-readable visual smoke check.
  DiscRenderer preview;
  auto preview_frame = frame(78, 18, dt, PlaybackState::Playing, 0.42, theme);
  step(preview, preview_frame, 20);
  ftxui::Screen preview_screen(78, 18);
  ftxui::Render(preview_screen, preview.render(preview_frame));
  std::cout << preview_screen.ToString() << "\n";
  std::cout << "disc: circle, rotation, theme label, tonearm states and resize "
               "passed\n";
}
