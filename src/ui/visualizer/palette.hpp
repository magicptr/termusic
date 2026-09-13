#pragma once

// Palette: how a height becomes a colour. Independent of the RENDERER -- the
// Spectrum is drawn in whichever ramp is selected, and a palette knows nothing
// about bars, gradients or the dot baseline. Adding one is one entry in the
// table plus one ramp function.

#include <string>
#include <string_view>
#include <vector>

#include <ftxui/screen/color.hpp>

#include "ui/theme.hpp"

namespace termusic::ui {

/// One named ramp.
struct VisualizerPalette {
  std::string_view id;
  std::string_view label;
  /// `intensity` is clamped to 0..1 by the caller's contract; the ramp is the
  /// only place that knows which colours a style can produce.
  ftxui::Color (*ramp)(float intensity, const Theme &theme);
};

/// The palette registry, in display order. `theme` follows the active UI
/// theme, so the default visualizer matches the rest of the screen.
const std::vector<VisualizerPalette> &visualizerPalettes();

/// The palette with this id, or the theme palette when it is unknown.
const VisualizerPalette &visualizerPalette(std::string_view id);

/// True when `id` names a palette in the registry.
bool isVisualizerPalette(std::string_view id);

/// The id a stored/typed value resolves to: a known id, else "theme".
std::string_view normalizeVisualizerPaletteId(std::string_view id);

/// The canonical palette id.
inline constexpr std::string_view kDefaultVisualizerPalette = "theme";

} // namespace termusic::ui
