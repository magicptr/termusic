#include "ui/visualizer/palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace termusic::ui {
namespace {

using ftxui::Color;

float clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }

/// The active theme's own spectrum ramp: quiet bands take the low accent, loud
/// ones the high accent. This is the default, and the only ramp that re-themes
/// with the rest of the UI.
Color themeRamp(float intensity, const Theme &theme) {
  return Color::Interpolate(clamp01(intensity), theme.spectrum_low,
                            theme.spectrum_high);
}

/// Cold: deep blue at the bottom of the scale, white at the top.
Color iceRamp(float intensity, const Theme &theme) {
  (void)theme;
  const float value = clamp01(intensity);
  const Color low = Color::RGB(0x2A, 0x3F, 0x8F);
  const Color mid = Color::RGB(0x53, 0xC0, 0xF0);
  const Color high = Color::RGB(0xEA, 0xF6, 0xFF);
  return value < 0.6F ? Color::Interpolate(value / 0.6F, low, mid)
                      : Color::Interpolate((value - 0.6F) / 0.4F, mid, high);
}

/// Warm: embers to a bright flame.
Color fireRamp(float intensity, const Theme &theme) {
  (void)theme;
  const float value = clamp01(intensity);
  const Color low = Color::RGB(0x5A, 0x18, 0x1E);
  const Color mid = Color::RGB(0xE0, 0x6C, 0x2A);
  const Color high = Color::RGB(0xFF, 0xE8, 0x9A);
  return value < 0.6F ? Color::Interpolate(value / 0.6F, low, mid)
                      : Color::Interpolate((value - 0.6F) / 0.4F, mid, high);
}

/// Hue rotation, kept at a fixed saturation/lightness so the ramp still reads
/// as one scale rather than a paint box.
Color rainbowRamp(float intensity, const Theme &theme) {
  (void)theme;
  const float value = clamp01(intensity);
  const float hue = (1.0F - value) * 280.0F; // violet -> red as it gets louder
  const float h = hue / 60.0F;
  const float c = 0.75F;
  const float x = c * (1.0F - std::abs(std::fmod(h, 2.0F) - 1.0F));
  float r = 0.0F, g = 0.0F, b = 0.0F;
  if (h < 1.0F)      { r = c; g = x; }
  else if (h < 2.0F) { r = x; g = c; }
  else if (h < 3.0F) { g = c; b = x; }
  else if (h < 4.0F) { g = x; b = c; }
  else if (h < 5.0F) { r = x; b = c; }
  else               { r = c; b = x; }
  const float lift = 0.18F + 0.22F * value;
  const auto channel = [lift](float component) {
    const long scaled = std::lround((component + lift) * 255.0F);
    return static_cast<std::uint8_t>(std::clamp(scaled, 0L, 255L));
  };
  return Color::RGB(channel(r), channel(g), channel(b));
}

const std::vector<VisualizerPalette> kPalettes = {
    {"theme", "Theme", themeRamp},
    {"ice", "Ice", iceRamp},
    {"fire", "Fire", fireRamp},
    {"rainbow", "Rainbow", rainbowRamp},
};

} // namespace

const std::vector<VisualizerPalette> &visualizerPalettes() { return kPalettes; }

bool isVisualizerPalette(std::string_view id) {
  return std::any_of(kPalettes.begin(), kPalettes.end(),
                     [id](const VisualizerPalette &palette) {
                       return palette.id == id;
                     });
}

const VisualizerPalette &visualizerPalette(std::string_view id) {
  for (const VisualizerPalette &palette : kPalettes) {
    if (palette.id == id)
      return palette;
  }
  return kPalettes.front();
}

std::string_view normalizeVisualizerPaletteId(std::string_view id) {
  return isVisualizerPalette(id) ? id : kDefaultVisualizerPalette;
}

} // namespace termusic::ui
