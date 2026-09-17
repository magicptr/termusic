#pragma once

#include <memory>

#include <ftxui/dom/elements.hpp>

#include "app/state.hpp"

namespace termusic::ui {

/// Horizontal cell size relative to its height in the reference terminal.
inline constexpr double kDiscCellAspect = 0.46;

struct DiscFrame {
  int columns = 0;
  int rows = 0;
  double dt = 1.0 / 15.0;
  PlaybackState playback = PlaybackState::Stopped;
};

struct DiscStats {
  int center_x = 0;
  int center_y = 0;
  int radius_rows = 0;
  int left = 0;
  int right = 0;
  int top = 0;
  int bottom = 0;
  double rotation_phase = 0.0;
  double tonearm_position = 1.0;
  double tonearm_bend = 0.0;
  double tonearm_head_radius = 0.0;
  bool geometry_fits = true;
};

/// Pixel-art reconstruction of the supplied record reference. The record and
/// its pivoted tonearm form one fixed composition; playback rotates only the
/// subtle groove texture.
class DiscRenderer {
public:
  DiscRenderer();
  ~DiscRenderer();
  DiscRenderer(DiscRenderer &&) noexcept;
  DiscRenderer &operator=(DiscRenderer &&) noexcept;

  void reset();
  void update(const DiscFrame &frame);
  ftxui::Element render(const DiscFrame &frame);
  DiscStats stats() const;
  bool animationActive() const;

private:
  struct Model;
  std::unique_ptr<Model> model_;
};

} // namespace termusic::ui
