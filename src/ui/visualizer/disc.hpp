#pragma once

#include <memory>
#include <string_view>

#include <ftxui/dom/elements.hpp>

#include "app/state.hpp"
#include "ui/theme.hpp"

namespace termusic::ui {

/// Terminal cells are roughly half as wide as they are tall. Geometry is
/// calculated in physical units with this scale so the vinyl reads as a circle.
inline constexpr double kDiscCellAspect = 0.46;

struct DiscFrame {
  int columns = 0;
  int rows = 0;
  double dt = 1.0 / 15.0;
  PlaybackState playback = PlaybackState::Stopped;
  double progress = 0.0;
  std::string_view track_identity;
  const Theme &theme;
};

struct DiscStats {
  int center_x = 0;
  int center_y = 0;
  int radius_rows = 0;
  int left = 0;
  int right = 0;
  double rotation_phase = 0.0;
  double arm_angle = 0.0;
  double contact_radius = 0.0;
  bool needle_on_disc = false;
  bool parked_outside = true;
  bool arm_clear_of_disc = true;
  bool arm_settled = true;
  bool geometry_fits = true;
};

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
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace termusic::ui
