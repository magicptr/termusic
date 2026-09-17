#include "ui/visualizer/disc.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "util/text.hpp"

namespace termusic::ui {
namespace {

using namespace ftxui;

constexpr double kTurnRate = 2.35;
constexpr double kArmTravelRate = 1.8;
constexpr double kTau = std::numbers::pi * 2.0;
constexpr double kLightAxis = 0.98;
constexpr double kArmPivotX = 1.05;
constexpr double kArmPivotY = -0.72;
// At rest the cartridge clears the record by only a few terminal cells.
constexpr double kArmParkAngle = -38.0 * std::numbers::pi / 180.0;

// Keep the record below the neutral metal arm in the value hierarchy. The
// grooves still have enough range to read while rotating, but no highlight is
// bright enough to turn the black vinyl grey.
const Color kVinylEdge = Color::RGB(5, 3, 10);
const Color kSpindle = Color::RGB(0, 0, 0);
const Color kArmShadow = Color::RGB(40, 39, 45);
const Color kArmDark = Color::RGB(68, 68, 70);
const Color kArmMid = Color::RGB(112, 109, 116);
const Color kArmLight = Color::RGB(174, 172, 181);
const Color kArmHighlight = Color::RGB(207, 206, 214);
const Color kArmPin = Color::RGB(160, 159, 166);

struct PaintCell {
  std::string glyph;
  Color foreground = Color::Default;
  int layer = 0;
};

double wrap(double value) {
  value = std::fmod(value, kTau);
  return value < 0.0 ? value + kTau : value;
}

std::uint8_t channel(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

Color graphite(double radius, double angle, double phase) {
  constexpr double kBandWidth = 0.082;
  const double band_position = (radius - 0.29) / kBandWidth;
  const int band = std::clamp(static_cast<int>(band_position), 0, 8);
  const double band_fraction = band_position - std::floor(band_position);
  const double axis = std::fabs(std::cos(angle - kLightAxis));
  const double broad_light = std::pow(axis, 5.2);
  const double band_step = (band % 2 == 0 ? 3.0 : -2.0) + band * 0.25;
  const double groove_cut = band_fraction < 0.16 ? -5.0 : 0.0;

  // A small travelling glint makes rotation legible without rotating the
  // reference's fixed upper-left/lower-right studio light.
  const double travelling = std::cos(angle - phase + band * 0.61);
  const double glint = travelling > 0.91 ? (travelling - 0.91) * 22.0 : 0.0;
  const double level =
      16.0 + broad_light * 47.0 + band_step + groove_cut + glint;
  const double cool = (1.0 - broad_light) * 1.2;
  return Color::RGB(channel(static_cast<int>(std::lround(level))),
                    channel(static_cast<int>(std::lround(level))),
                    channel(static_cast<int>(std::lround(level + cool))));
}

std::string boundaryGlyph(double angle) {
  const double vertical = std::sin(angle);
  if (vertical < -0.42)
    return "▄";
  if (vertical > 0.42)
    return "▀";
  return "█";
}

std::string roundInsetGlyph(double angle) {
  const double horizontal = std::cos(angle);
  const double vertical = std::sin(angle);
  if (std::fabs(vertical) >= std::fabs(horizontal))
    return vertical < 0.0 ? "▄" : "▀";
  return horizontal < 0.0 ? "▐" : "▌";
}

} // namespace

struct DiscRenderer::Model {
  int columns = 0;
  int rows = 0;
  int center_x = 0;
  int center_y = 0;
  int radius = 0;
  double phase = 0.0;
  double arm_position = 1.0;
  PlaybackState playback = PlaybackState::Stopped;
  std::vector<double> radii;
  std::vector<double> angles;
  std::vector<PaintCell> canvas;
  DiscStats stats;

  void layout(int new_columns, int new_rows) {
    columns = std::max(0, new_columns);
    rows = std::max(0, new_rows);

    // Measured from the reference: the disc is 68% of the framed width and
    // occupies roughly 80% of the visual region (59% of the complete screen).
    const int height_radius =
        static_cast<int>(std::lround(static_cast<double>(rows) * 0.40));
    const int width_radius = static_cast<int>(std::lround(
        static_cast<double>(columns) * kDiscCellAspect * 0.34));
    radius = std::max(1, std::min(height_radius, width_radius));
    center_x = columns / 2;
    center_y = std::max(radius, (rows - 1) / 2);

    const std::size_t size = static_cast<std::size_t>(columns) *
                             static_cast<std::size_t>(rows);
    radii.assign(size, 99.0);
    angles.assign(size, 0.0);
    canvas.assign(size, PaintCell{});

    for (int y = 0; y < rows; ++y) {
      for (int x = 0; x < columns; ++x) {
        const double dx = static_cast<double>(x - center_x) * kDiscCellAspect;
        const double dy = static_cast<double>(y - center_y);
        const std::size_t index = static_cast<std::size_t>(y * columns + x);
        radii[index] = std::hypot(dx, dy) / static_cast<double>(radius);
        angles[index] = std::atan2(dy, dx);
      }
    }
  }

  void paint(int x, int y, std::string glyph, Color color, int layer) {
    if (x < 0 || x >= columns || y < 0 || y >= rows)
      return;
    PaintCell &cell = canvas[static_cast<std::size_t>(y * columns + x)];
    if (layer < cell.layer)
      return;
    cell = {std::move(glyph), color, layer};
  }

  void paintCircle(double normalized_x, double normalized_y,
                   double normalized_radius, Color color, int layer) {
    for (int y = 0; y < rows; ++y) {
      for (int x = 0; x < columns; ++x) {
        const double px =
            static_cast<double>(x - center_x) * kDiscCellAspect / radius;
        const double py = static_cast<double>(y - center_y) / radius;
        if (std::hypot(px - normalized_x, py - normalized_y) <=
            normalized_radius)
          paint(x, y, "█", color, layer);
      }
    }
  }

  void paintPoint(double normalized_x, double normalized_y, std::string glyph,
                  Color color, int layer) {
    const int x = center_x + static_cast<int>(std::lround(
                                 normalized_x * radius / kDiscCellAspect));
    const int y =
        center_y + static_cast<int>(std::lround(normalized_y * radius));
    paint(x, y, std::move(glyph), color, layer);
  }

  void paintSegment(double ax, double ay, double bx, double by,
                    double thickness, Color color, int layer,
                    const std::string &glyph = "█") {
    const double vx = bx - ax;
    const double vy = by - ay;
    const double length_squared = vx * vx + vy * vy;
    for (int y = 0; y < rows; ++y) {
      for (int x = 0; x < columns; ++x) {
        const double px =
            static_cast<double>(x - center_x) * kDiscCellAspect / radius;
        const double py = static_cast<double>(y - center_y) / radius;
        const double projection =
            length_squared > 0.0
                ? std::clamp(((px - ax) * vx + (py - ay) * vy) /
                                 length_squared,
                             0.0, 1.0)
                : 0.0;
        const double nearest_x = ax + projection * vx;
        const double nearest_y = ay + projection * vy;
        if (std::hypot(px - nearest_x, py - nearest_y) <= thickness)
          paint(x, y, glyph, color, layer);
      }
    }
  }

  void paintBlockLine(double ax, double ay, double bx, double by, Color color,
                      int layer) {
    int x = center_x + static_cast<int>(std::lround(
                           ax * radius / kDiscCellAspect));
    int y = center_y + static_cast<int>(std::lround(ay * radius));
    const int end_x = center_x + static_cast<int>(std::lround(
                                     bx * radius / kDiscCellAspect));
    const int end_y = center_y + static_cast<int>(std::lround(by * radius));
    const int dx = std::abs(end_x - x);
    const int step_x = x < end_x ? 1 : -1;
    const int dy = -std::abs(end_y - y);
    const int step_y = y < end_y ? 1 : -1;
    int error = dx + dy;
    while (true) {
      paint(x, y, "█", color, layer);
      if (x == end_x && y == end_y)
        break;
      const int doubled = error * 2;
      if (doubled >= dy) {
        error += dy;
        x += step_x;
      }
      if (doubled <= dx) {
        error += dx;
        y += step_y;
      }
    }
  }

  Element row(int y) const {
    Elements runs;
    for (int x = 0; x < columns;) {
      const PaintCell &first =
          canvas[static_cast<std::size_t>(y * columns + x)];
      int end = x + 1;
      while (end < columns) {
        const PaintCell &next =
            canvas[static_cast<std::size_t>(y * columns + end)];
        if (next.glyph != first.glyph || next.foreground != first.foreground)
          break;
        ++end;
      }
      Element run = text(util::repeat(end - x, first.glyph.empty() ? " "
                                                                  : first.glyph));
      if (!first.glyph.empty())
        run = std::move(run) | color(first.foreground);
      runs.push_back(std::move(run));
      x = end;
    }
    return hbox(std::move(runs));
  }
};

DiscRenderer::DiscRenderer() : model_(std::make_unique<Model>()) {}
DiscRenderer::~DiscRenderer() = default;
DiscRenderer::DiscRenderer(DiscRenderer &&) noexcept = default;
DiscRenderer &DiscRenderer::operator=(DiscRenderer &&) noexcept = default;

void DiscRenderer::reset() { model_ = std::make_unique<Model>(); }

void DiscRenderer::update(const DiscFrame &frame) {
  Model &m = *model_;
  if (frame.columns != m.columns || frame.rows != m.rows)
    m.layout(frame.columns, frame.rows);
  m.playback = frame.playback;
  if (frame.playback == PlaybackState::Playing)
    m.phase = wrap(m.phase + kTurnRate * std::clamp(frame.dt, 0.0, 0.12));
  const double arm_target =
      frame.playback == PlaybackState::Playing ? 0.0 : 1.0;
  const double arm_step = kArmTravelRate * std::clamp(frame.dt, 0.0, 0.12);
  if (m.arm_position < arm_target)
    m.arm_position = std::min(arm_target, m.arm_position + arm_step);
  else if (m.arm_position > arm_target)
    m.arm_position = std::max(arm_target, m.arm_position - arm_step);
}

Element DiscRenderer::render(const DiscFrame &frame) {
  Model &m = *model_;
  if (frame.columns != m.columns || frame.rows != m.rows)
    m.layout(frame.columns, frame.rows);
  std::fill(m.canvas.begin(), m.canvas.end(), PaintCell{});

  // The paper label is the only themed part of the record. Its brightest face
  // is the theme accent itself; the other two faces are derived from it, so a
  // theme switch preserves the pixel-art lighting instead of flattening the
  // centre into one solid colour.
  const Color label_light = frame.theme.accent_primary;
  const Color label_main =
      Color::Interpolate(0.12F, label_light, Color::Black);
  const Color label_shade =
      Color::Interpolate(0.28F, label_light, Color::Black);

  for (int y = 0; y < m.rows; ++y) {
    for (int x = 0; x < m.columns; ++x) {
      const std::size_t index = static_cast<std::size_t>(y * m.columns + x);
      const double radius = m.radii[index];
      const double angle = m.angles[index];
      if (radius > 1.02)
        continue;

      // Continue the innermost groove shading all the way underneath the
      // centre label. Previously 0.285 < radius < 0.29 used a separate nearly
      // black core colour; terminal-cell quantisation turned that tiny annulus
      // into conspicuous black squares above, below or beside the label at
      // certain responsive sizes. The label still paints over the centre at a
      // higher layer, while every exposed neighbour now belongs to the same
      // graphite surface as the rest of the record.
      if (radius <= 0.96) {
        m.paint(x, y, "█", graphite(radius, angle, m.phase), 3);
      } else {
        m.paint(x, y, radius > 0.97 ? boundaryGlyph(angle) : "█", kVinylEdge,
                2);
      }

      if (radius <= 0.285) {
        const double light = std::cos(angle + 2.25);
        const Color label = light > 0.42    ? label_light
                            : light < -0.35 ? label_shade
                                           : label_main;
        m.paint(x, y, "█", label, 5);
      }
      constexpr double kSpindleRadius = 0.060;
      constexpr double kSpindleSolidRadius = 0.025;
      if (radius <= kSpindleRadius) {
        m.paint(x, y,
                radius > kSpindleSolidRadius ? roundInsetGlyph(angle) : "█",
                kSpindle, 7);
      }
    }
  }

  // The arm is a one-cell-wide chain of square pixels. Whenever music is not
  // playing it rotates clockwise into the clear area to the record's right.
  const double t = m.arm_position;
  const double eased_arm =
      t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
  const double arm_angle = kArmParkAngle * eased_arm;
  const double arm_cos = std::cos(arm_angle);
  const double arm_sin = std::sin(arm_angle);
  const auto rotate_from_pivot = [&](double local_x, double local_y) {
    return std::array{
        kArmPivotX + local_x * arm_cos - local_y * arm_sin,
        kArmPivotY + local_x * arm_sin + local_y * arm_cos,
    };
  };
  // A cubic curve avoids visible elbows. It is sampled before rotation, so
  // every animation frame is the exact same bent arm at a different angle.
  constexpr std::array<double, 2> kCurveStart{-0.015, 0.11};
  constexpr std::array<double, 2> kCurveControlA{-0.015, 0.26};
  constexpr std::array<double, 2> kCurveControlB{-0.10, 0.43};
  constexpr std::array<double, 2> kCurveEnd{-0.22, 0.45};
  constexpr std::size_t kCurveSamples = 12;
  std::array<std::array<double, 2>, kCurveSamples> arm_path{};
  for (std::size_t point = 0; point < arm_path.size(); ++point) {
    const double curve_t = static_cast<double>(point) /
                           static_cast<double>(arm_path.size() - 1);
    const double inverse = 1.0 - curve_t;
    const double start_weight = inverse * inverse * inverse;
    const double control_a_weight = 3.0 * inverse * inverse * curve_t;
    const double control_b_weight = 3.0 * inverse * curve_t * curve_t;
    const double end_weight = curve_t * curve_t * curve_t;
    const double local_x = start_weight * kCurveStart[0] +
                           control_a_weight * kCurveControlA[0] +
                           control_b_weight * kCurveControlB[0] +
                           end_weight * kCurveEnd[0];
    const double local_y = start_weight * kCurveStart[1] +
                           control_a_weight * kCurveControlA[1] +
                           control_b_weight * kCurveControlB[1] +
                           end_weight * kCurveEnd[1];
    arm_path[point] = rotate_from_pivot(local_x, local_y);
  }
  for (std::size_t point = 1; point < arm_path.size(); ++point) {
    const auto &from = arm_path[point - 1];
    const auto &to = arm_path[point];
    m.paintBlockLine(from[0], from[1], to[0], to[1], kArmLight, 10);
  }

  // Pivot: graphite surround, metal ring and a neutral steel pin. Every colour
  // is fixed here; the application theme never enters the disc renderer.
  m.paintCircle(kArmPivotX, kArmPivotY, 0.105, kArmShadow, 11);
  m.paintCircle(kArmPivotX, kArmPivotY, 0.080, kArmMid, 12);
  m.paintCircle(kArmPivotX, kArmPivotY, 0.048, kArmDark, 13);
  m.paintCircle(kArmPivotX, kArmPivotY, 0.022, kArmPin, 14);
  m.paintCircle(kArmPivotX + 0.030, kArmPivotY - 0.040, 0.018,
                kArmHighlight, 15);
  m.paintPoint(kArmPivotX, kArmPivotY, "█", kArmPin, 16);
  m.paintPoint(kArmPivotX + 0.09, kArmPivotY, "█", kArmShadow, 16);

  // The cartridge is a compact block at the end of the arm. There is no
  // separately drawn needle: at terminal resolution it only makes the arm
  // look artificially thick.
  const auto cartridge_a = rotate_from_pivot(-0.22, 0.45);
  const auto cartridge_b = rotate_from_pivot(-0.27, 0.50);
  const double cartridge_outline = std::max(0.043, 0.42 / m.radius);
  const double cartridge_fill = std::max(0.026, 0.28 / m.radius);
  m.paintSegment(cartridge_a[0], cartridge_a[1], cartridge_b[0],
                 cartridge_b[1],
                 cartridge_outline, kArmShadow, 11);
  m.paintSegment(cartridge_a[0], cartridge_a[1], cartridge_b[0],
                 cartridge_b[1],
                 cartridge_fill, kArmMid, 12);
  m.paintPoint(std::lerp(cartridge_a[0], cartridge_b[0], 0.48),
               std::lerp(cartridge_a[1], cartridge_b[1], 0.48), "█",
               kArmPin, 13);

  const auto &arm_start = arm_path.front();
  const auto &arm_middle = arm_path[arm_path.size() / 2];
  const auto &arm_end = arm_path.back();
  const double arm_chord_x = arm_end[0] - arm_start[0];
  const double arm_chord_y = arm_end[1] - arm_start[1];
  const double arm_chord_length = std::hypot(arm_chord_x, arm_chord_y);
  const double arm_bend =
      arm_chord_length > 0.0
          ? std::fabs((arm_middle[0] - arm_start[0]) * arm_chord_y -
                      (arm_middle[1] - arm_start[1]) * arm_chord_x) /
                arm_chord_length
          : 0.0;

  const int horizontal_radius =
      static_cast<int>(std::ceil(m.radius / kDiscCellAspect));
  m.stats = {
      .center_x = m.center_x,
      .center_y = m.center_y,
      .radius_rows = m.radius,
      .left = m.center_x - horizontal_radius,
      .right = m.center_x + horizontal_radius,
      .top = m.center_y - m.radius,
      .bottom = m.center_y + m.radius,
      .rotation_phase = m.phase,
      .tonearm_position = m.arm_position,
      .tonearm_bend = arm_bend,
      .tonearm_head_radius = std::hypot(cartridge_b[0], cartridge_b[1]),
      .geometry_fits = m.center_x - horizontal_radius >= 0 &&
                       m.center_x + static_cast<int>(std::ceil(
                                          m.radius * 1.38 / kDiscCellAspect)) <
                           m.columns &&
                       m.center_y - m.radius >= 0 &&
                       m.center_y + m.radius < m.rows,
  };

  Elements rows;
  rows.reserve(static_cast<std::size_t>(m.rows));
  for (int y = 0; y < m.rows; ++y)
    rows.push_back(m.row(y));
  return vbox(std::move(rows));
}

DiscStats DiscRenderer::stats() const { return model_->stats; }

bool DiscRenderer::animationActive() const {
  const double target =
      model_->playback == PlaybackState::Playing ? 0.0 : 1.0;
  return model_->playback == PlaybackState::Playing ||
         std::fabs(model_->arm_position - target) > 0.0001;
}

} // namespace termusic::ui
