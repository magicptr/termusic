#include "ui/visualizer/disc.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "util/text.hpp"

namespace termusic::ui {
namespace {

using namespace ftxui;

constexpr double kRotationRadiansPerSecond = 3.15;
constexpr double kOuterPlayable = 0.76;
constexpr double kInnerPlayable = 0.52;
constexpr double kArmLength = 0.65;
constexpr double kPivotX = 1.00;
constexpr double kPivotY = -0.50;
constexpr double kParkAngle = 1.20;
constexpr double kOnDiscResponse = 8.0;
constexpr double kProgressResponse = 4.0;
constexpr double kRetractResponse = 6.0;
/// Block geometry makes a large first exponential step look like a teleport.
/// Cap angular travel so each 15 FPS frame advances only a small staircase.
constexpr double kArmMaxRadiansPerSecond = 1.80;
constexpr double kTwoPi = std::numbers::pi * 2.0;

struct Cell {
  std::string glyph;
  Color color = Color::Default;
  int priority = 0;
};

double wrapPhase(double phase) {
  phase = std::fmod(phase, kTwoPi);
  return phase < 0.0 ? phase + kTwoPi : phase;
}

double angleDistance(double from, double to) {
  return std::remainder(to - from, kTwoPi);
}

std::string discBlock(double angle) {
  const double horizontal = std::fabs(std::cos(angle));
  const double vertical = std::fabs(std::sin(angle));
  if (horizontal > vertical * 1.8)
    return "█";
  return std::sin(angle) < 0.0 ? "▄" : "▀";
}

} // namespace

struct DiscRenderer::Impl {
  int columns = 0;
  int rows = 0;
  int center_x = 0;
  int center_y = 0;
  int radius = 0;
  int pivot_x = 0;
  int pivot_y = 0;
  double pivot_px = 0.0;
  double pivot_py = 0.0;
  double arm_length = 0.0;
  double phase = 0.0;
  double arm_angle = kParkAngle;
  double target_angle = kParkAngle;
  PlaybackState playback = PlaybackState::Stopped;
  std::string track;
  double last_progress = 0.0;
  bool first_update = true;
  bool track_changed = false;
  std::vector<double> normalized_radius;
  std::vector<double> polar_angle;
  std::vector<Cell> cells;
  std::vector<Cell> row;
  DiscStats stats;

  void rebuild(int new_columns, int new_rows) {
    columns = std::max(0, new_columns);
    rows = std::max(0, new_rows);
    // Size the vinyl first. Its centre is the composition's visual anchor;
    // the tonearm is allowed to overlap its right-hand bay instead of pulling
    // the whole record far to the left.
    radius = std::max(1, std::min(
        static_cast<int>(static_cast<double>(rows - 1) / 2.04),
        static_cast<int>(static_cast<double>(columns - 2) * kDiscCellAspect /
                         3.55)));
    const int gentle_left_shift = static_cast<int>(std::lround(
        0.08 * static_cast<double>(radius) / kDiscCellAspect));
    center_x = columns / 2 - gentle_left_shift;
    center_y = rows / 2;
    pivot_px = static_cast<double>(center_x) * kDiscCellAspect +
               kPivotX * static_cast<double>(radius);
    pivot_py = static_cast<double>(center_y) +
               kPivotY * static_cast<double>(radius);
    pivot_x = static_cast<int>(std::lround(pivot_px / kDiscCellAspect));
    pivot_y = static_cast<int>(std::lround(pivot_py));
    arm_length = kArmLength * static_cast<double>(radius);

    const std::size_t count = static_cast<std::size_t>(columns) *
                              static_cast<std::size_t>(rows);
    normalized_radius.assign(count, 99.0);
    polar_angle.assign(count, 0.0);
    cells.assign(count, Cell{});
    row.assign(static_cast<std::size_t>(columns), Cell{});
    for (int y = 0; y < rows; ++y) {
      for (int x = 0; x < columns; ++x) {
        const double dx = (static_cast<double>(x - center_x) * kDiscCellAspect);
        const double dy = static_cast<double>(y - center_y);
        const std::size_t at = static_cast<std::size_t>(y * columns + x);
        normalized_radius[at] =
            std::hypot(dx, dy) / static_cast<double>(std::max(1, radius));
        polar_angle[at] = std::atan2(dy, dx);
      }
    }
  }

  double grooveAngle(double progress) const {
    const double groove_radius =
        std::lerp(kOuterPlayable, kInnerPlayable,
                  std::clamp(progress, 0.0, 1.0)) *
        static_cast<double>(radius);
    const double dx = pivot_px - static_cast<double>(center_x) * kDiscCellAspect;
    const double dy = pivot_py - static_cast<double>(center_y);
    const double distance = std::hypot(dx, dy);
    if (distance <= 0.0)
      return kParkAngle;
    const double along = (groove_radius * groove_radius -
                          arm_length * arm_length + distance * distance) /
                         (2.0 * distance);
    const double height =
        std::sqrt(std::max(0.0, groove_radius * groove_radius - along * along));
    const double ux = dx / distance;
    const double uy = dy / distance;
    // Choose the lower-right intersection, matching a conventional tonearm.
    const double needle_x =
        static_cast<double>(center_x) * kDiscCellAspect + along * ux - height * uy;
    const double needle_y =
        static_cast<double>(center_y) + along * uy + height * ux;
    return std::atan2(needle_y - pivot_py, needle_x - pivot_px);
  }

  void setCell(int x, int y, std::string glyph, Color color, int priority) {
    if (x < 0 || x >= columns || y < 0 || y >= rows)
      return;
    Cell &cell = cells[static_cast<std::size_t>(y * columns + x)];
    if (priority < cell.priority)
      return;
    cell = Cell{std::move(glyph), color, priority};
  }

  void blockLine(int x0, int y0, int x1, int y1, Color color,
                 std::string_view glyph = "█", int priority = 8) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
      setCell(x0, y0, std::string(glyph), color, priority);
      if (x0 == x1 && y0 == y1)
        break;
      const int twice = 2 * error;
      const bool move_x = twice >= dy;
      const bool move_y = twice <= dx;
      // A diagonal cell touches only at a corner. Fill one orthogonal bridge
      // so the arm is a solid staircase of terminal blocks, never a dotted
      // diagonal or a line-glyph approximation.
      if (move_x && move_y)
        setCell(x0 + sx, y0, std::string(glyph), color, priority);
      if (move_x) {
        error += dy;
        x0 += sx;
      }
      if (move_y) {
        error += dx;
        y0 += sy;
      }
    }
  }

  Element rowElement(int y) {
    Elements runs;
    for (int x = 0; x < columns;) {
      const Cell &cell = cells[static_cast<std::size_t>(y * columns + x)];
      int end = x + 1;
      while (end < columns) {
        const Cell &next = cells[static_cast<std::size_t>(y * columns + end)];
        if (next.glyph != cell.glyph || next.color != cell.color)
          break;
        ++end;
      }
      const std::string glyph = cell.glyph.empty() ? " " : cell.glyph;
      Element run = text(util::repeat(end - x, glyph));
      if (!cell.glyph.empty())
        run = std::move(run) | color(cell.color);
      runs.push_back(std::move(run));
      x = end;
    }
    return hbox(std::move(runs));
  }
};

DiscRenderer::DiscRenderer() : impl_(std::make_unique<Impl>()) {}
DiscRenderer::~DiscRenderer() = default;
DiscRenderer::DiscRenderer(DiscRenderer &&) noexcept = default;
DiscRenderer &DiscRenderer::operator=(DiscRenderer &&) noexcept = default;

void DiscRenderer::reset() { impl_ = std::make_unique<Impl>(); }

void DiscRenderer::update(const DiscFrame &frame) {
  Impl &s = *impl_;
  if (frame.columns != s.columns || frame.rows != s.rows)
    s.rebuild(frame.columns, frame.rows);
  const double dt = std::clamp(frame.dt, 0.0, 0.12);
  if (frame.playback == PlaybackState::Playing)
    s.phase = wrapPhase(s.phase + kRotationRadiansPerSecond * dt);

  const double progress = std::clamp(frame.progress, 0.0, 1.0);
  const bool restarted = frame.playback != PlaybackState::Stopped &&
                         progress + 0.05 < s.last_progress;
  s.track_changed = restarted ||
                    (!s.track.empty() && frame.track_identity != s.track);
  if (!frame.track_identity.empty())
    s.track = std::string(frame.track_identity);
  const PlaybackState previous_playback = s.playback;
  s.playback = frame.playback;
  const bool parking = frame.playback != PlaybackState::Playing;
  const bool engaging = previous_playback != PlaybackState::Playing &&
                        frame.playback == PlaybackState::Playing;
  s.target_angle = parking ? kParkAngle : s.grooveAngle(progress);
  double response = parking
                        ? kRetractResponse
                        : (s.first_update || s.track_changed || engaging
                               ? kOnDiscResponse
                               : kProgressResponse);
  const double blend = 1.0 - std::exp(-response * dt);
  const double desired_step =
      angleDistance(s.arm_angle, s.target_angle) * blend;
  const double max_step = kArmMaxRadiansPerSecond * dt;
  s.arm_angle += std::clamp(desired_step, -max_step, max_step);
  s.arm_angle = wrapPhase(s.arm_angle);
  s.last_progress = progress;
  s.first_update = false;
}

Element DiscRenderer::render(const DiscFrame &frame) {
  Impl &s = *impl_;
  if (frame.columns != s.columns || frame.rows != s.rows)
    s.rebuild(frame.columns, frame.rows);
  std::fill(s.cells.begin(), s.cells.end(), Cell{});
  const bool compact = s.radius < 6;
  const int groove_count = compact ? 4 : (s.radius >= 10 ? 6 : 5);

  for (int y = 0; y < s.rows; ++y) {
    for (int x = 0; x < s.columns; ++x) {
      const std::size_t at = static_cast<std::size_t>(y * s.columns + x);
      const double r = s.normalized_radius[at];
      if (r > 1.025)
        continue;
      // The vinyl is a filled object distinct from the terminal background.
      // `panel` stays dark in every theme but does not disappear into empty
      // space the way `background_deep` did.
      s.setCell(x, y, "█", frame.theme.panel, 1);
      // The reference has one restrained dark rim, not a bright outline.
      if (r > 0.985) {
        const double rim_light = std::fabs(std::sin(s.polar_angle[at]));
        const Color rim_color = rim_light > 0.84
                                    ? frame.theme.border
                                : rim_light > 0.52 ? frame.theme.surface
                                                   : frame.theme.background_deep;
        s.setCell(x, y, discBlock(s.polar_angle[at]), rim_color, 5);
      }

      if (r >= 0.27 && r <= 0.94) {
        const double ring = (r - 0.27) / 0.67 * groove_count;
        const int ring_index = static_cast<int>(std::floor(ring));
        const double within = ring - std::floor(ring);
        // Broad, separated grey bands match the compact pixel reference much
        // better than many independent dashes. Dark gaps keep each concentric
        // groove legible even when the terminal palette has low contrast.
        {
          const double vertical = std::fabs(std::sin(s.polar_angle[at]));
          Color groove_color = frame.theme.background_deep;
          if (within >= 0.12) {
            if (vertical > 0.88)
              groove_color = ring_index % 2 == 0 ? frame.theme.weak_text
                                                  : frame.theme.border;
            else if (vertical > 0.62)
              groove_color = frame.theme.border;
            else if (vertical > 0.34)
              groove_color = frame.theme.surface;
            else
              groove_color = frame.theme.border_dim;
          }
          // Rotation is deliberately subtle: it only promotes a short piece
          // by one nearby grey role, preserving the reference's lighting.
          const double moving = std::fabs(std::remainder(
              s.polar_angle[at] - s.phase * 0.72 + ring_index * 0.51,
              kTwoPi));
          if (within >= 0.12 && moving < 0.16 && vertical > 0.34 &&
              vertical <= 0.62)
            groove_color = frame.theme.border;
          s.setCell(x, y, discBlock(s.polar_angle[at]), groove_color, 4);
        }
      }

      // Circular theme label, using the same physical radius correction as
      // the vinyl. A half-block boundary softens its top and bottom pixels.
      if (r <= 0.27)
        s.setCell(x, y, "█", frame.theme.accent_primary, 6);
      else if (r <= 0.30)
        s.setCell(x, y, discBlock(s.polar_angle[at]),
                  frame.theme.accent_primary, 6);
    }
  }

  // The reference label is completely clean: one dark spindle and no second
  // marker that could be mistaken for a missing pixel.
  s.setCell(s.center_x, s.center_y, "█", frame.theme.background_deep, 8);

  const double needle_px = s.pivot_px + s.arm_length * std::cos(s.arm_angle);
  const double needle_py = s.pivot_py + s.arm_length * std::sin(s.arm_angle);
  const int needle_x = static_cast<int>(std::lround(needle_px / kDiscCellAspect));
  const int needle_y = static_cast<int>(std::lround(needle_py));
  const double vx = needle_px - s.pivot_px;
  const double vy = needle_py - s.pivot_py;
  const double length = std::max(0.001, std::hypot(vx, vy));
  const double ux = vx / length;
  const double uy = vy / length;

  // The rest still defines the parked geometry, but is not drawn: the compact
  // reference has only the two-step light arm at the upper-right.
  const double parked_px = s.pivot_px + s.arm_length * std::cos(kParkAngle);
  const double parked_py = s.pivot_py + s.arm_length * std::sin(kParkAngle);
  const int rest_x = static_cast<int>(std::lround(parked_px / kDiscCellAspect));
  const int rest_y = static_cast<int>(std::lround(parked_py));
  // Two-step bright pivot, matching the reference's small white block shape.
  if (!compact) {
    s.setCell(s.pivot_x, s.pivot_y, "█", frame.theme.text, 12);
    s.setCell(s.pivot_x + 1, s.pivot_y, "█", frame.theme.text, 12);
    s.setCell(s.pivot_x + 1, s.pivot_y - 1, "█", frame.theme.text, 12);
  }
  // One compact half-block tube: solid enough to read, but no longer a large
  // diagonal bar competing with the record.
  s.blockLine(s.pivot_x, s.pivot_y, needle_x, needle_y,
              frame.theme.border_dim,
              "▄", 10);

  // No visible counterweight: it was the source of the fragmented shape in
  // the previous render and is absent from the supplied pixel reference.
  int counter_x = s.pivot_x;
  int counter_y = s.pivot_y;
  s.setCell(s.pivot_x, s.pivot_y, "█", frame.theme.text, 14);

  // Headshell is a connected final segment aligned with the tube. The
  // cartridge hangs from it, while the bright stylus dot is the exact point
  // used by the groove geometry and progress calculation.
  const double shell_length = (compact ? 0.14 : 0.22) * s.radius;
  const int shell_x = static_cast<int>(std::lround(
      (needle_px - ux * shell_length) / kDiscCellAspect));
  const int shell_y =
      static_cast<int>(std::lround(needle_py - uy * shell_length));
  s.blockLine(shell_x, shell_y, needle_x, needle_y,
              frame.theme.surface, "▄", 13);
  const int cartridge_x = static_cast<int>(std::lround(
      (needle_px - ux * 0.035 * s.radius - uy * 0.055 * s.radius) /
      kDiscCellAspect));
  const int cartridge_y = static_cast<int>(std::lround(
      needle_py - uy * 0.035 * s.radius + ux * 0.055 * s.radius));
  s.setCell(cartridge_x, cartridge_y, "▄", frame.theme.border, 14);
  s.setCell(needle_x, needle_y, "▄", frame.theme.border, 15);

  const double disc_dx = needle_px -
                         static_cast<double>(s.center_x) * kDiscCellAspect;
  const double disc_dy = needle_py - static_cast<double>(s.center_y);
  const double needle_radius =
      std::hypot(disc_dx, disc_dy) / static_cast<double>(std::max(1, s.radius));
  // Closest point from the record centre to the complete pivot->stylus
  // segment. A stopped arm is considered parked only when the entire tube,
  // not merely its cartridge, has cleared the vinyl silhouette.
  const double center_px = static_cast<double>(s.center_x) * kDiscCellAspect;
  const double center_py = static_cast<double>(s.center_y);
  const double segment_x = needle_px - s.pivot_px;
  const double segment_y = needle_py - s.pivot_py;
  const double segment_length_sq =
      segment_x * segment_x + segment_y * segment_y;
  const double closest_t = segment_length_sq > 0.0
                               ? std::clamp(((center_px - s.pivot_px) * segment_x +
                                             (center_py - s.pivot_py) * segment_y) /
                                                segment_length_sq,
                                            0.0, 1.0)
                               : 0.0;
  const double closest_x = s.pivot_px + closest_t * segment_x;
  const double closest_y = s.pivot_py + closest_t * segment_y;
  const double arm_clearance =
      std::hypot(closest_x - center_px, closest_y - center_py) /
      static_cast<double>(std::max(1, s.radius));
  s.stats.center_x = s.center_x;
  s.stats.center_y = s.center_y;
  s.stats.radius_rows = s.radius;
  s.stats.left = s.center_x - static_cast<int>(std::ceil(s.radius / kDiscCellAspect));
  s.stats.right = s.center_x + static_cast<int>(std::ceil(s.radius / kDiscCellAspect));
  s.stats.rotation_phase = s.phase;
  s.stats.arm_angle = s.arm_angle;
  s.stats.contact_radius = needle_radius;
  s.stats.needle_on_disc = needle_radius >= 0.32 && needle_radius <= 0.86;
  s.stats.parked_outside = needle_radius > 1.05;
  // Include the visible tube thickness, not only its mathematical centreline.
  s.stats.arm_clear_of_disc = arm_clearance > 1.10;
  s.stats.arm_settled =
      std::fabs(angleDistance(s.arm_angle, s.target_angle)) <= 0.006;
  s.stats.geometry_fits = s.stats.left >= 0 && s.stats.right < s.columns &&
                          s.pivot_x >= 0 && s.pivot_x < s.columns &&
                          needle_x >= 0 && needle_x < s.columns &&
                          needle_y >= 0 && needle_y < s.rows &&
                          (compact ||
                           (rest_x - 1 >= 0 && rest_x + 1 < s.columns &&
                            rest_y >= 0 && rest_y + 1 < s.rows &&
                            counter_x >= 0 && counter_x < s.columns &&
                            counter_y >= 0 && counter_y < s.rows));

  Elements output;
  output.reserve(static_cast<std::size_t>(s.rows));
  for (int y = 0; y < s.rows; ++y)
    output.push_back(s.rowElement(y));
  return vbox(std::move(output));
}

DiscStats DiscRenderer::stats() const { return impl_->stats; }

bool DiscRenderer::animationActive() const {
  const Impl &s = *impl_;
  return s.playback == PlaybackState::Playing ||
         std::fabs(angleDistance(s.arm_angle, s.target_angle)) > 0.006;
}

} // namespace termusic::ui
