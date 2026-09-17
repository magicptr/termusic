#include "ui/core/core.hpp"
#include "ui/core/settings.hpp"
#include "ui/visualizer/palette.hpp"

#include <string>
#include <vector>

namespace termusic::ui::core {
namespace {

/// Appearance: how the application looks, and what the display area shows.
///
/// The theme, the palette and the visualizer controls used to be hand-built
/// sliders and buttons; they are setting items now, so this file contains no
/// rendering code at all.
class AppearanceSection final : public SettingsListSection {
public:
  std::string_view title() const override { return "Appearance"; }

protected:
  void fill(const CoreContext &context) override {
    std::vector<std::string> themes;
    themes.reserve(context.themes.list().size());
    for (const ThemeInfo &info : context.themes.list())
      themes.push_back(info.id);

    // The palette is a choice of its own: the display draws in any of the
    // ramps, and the list comes from its registry, so a new ramp appears here
    // without touching this file.
    std::vector<std::string> palettes;
    std::vector<std::string> palette_ids;
    for (const termusic::ui::VisualizerPalette &palette :
         termusic::ui::visualizerPalettes()) {
      palettes.emplace_back(palette.label);
      palette_ids.emplace_back(palette.id);
    }

    std::vector<SettingItem> items;
    items.push_back(heading("Theme"));
    items.push_back(select(
        "Theme", std::move(themes),
        [this] {
          const std::string active =
              context_->themes.resolveId(context_->config().theme_name);
          const auto &all = context_->themes.list();
          for (std::size_t index = 0; index < all.size(); ++index) {
            if (all[index].id == active)
              return static_cast<int>(index);
          }
          return 0;
        },
        [this](int index) {
          const auto &all = context_->themes.list();
          if (all.empty())
            return;
          const std::size_t at = static_cast<std::size_t>(index) % all.size();
          context_->controller.config().theme_name = all[at].id;
          context_->save_config();
          // The Application owns the live Theme: it re-resolves and repaints.
          context_->theme_changed();
        },
        "Seven presets ship with Termusic; Catppuccin Mocha is the default."));
    items.push_back(heading("Display"));
    items.push_back(toggle(
        "Spectrum",
        [this] {
          return context_->state.display_mode == DisplayMode::Spectrum;
        },
        [this](bool value) {
          if (value)
            context_->state.display_mode = DisplayMode::Spectrum;
        },
        "Show the music spectrum visualization."));
    items.push_back(toggle(
        "Disc",
        [this] { return context_->state.display_mode == DisplayMode::Disc; },
        [this](bool value) {
          if (value)
            context_->state.display_mode = DisplayMode::Disc;
        },
        "Show an animated vinyl record and tonearm."));
    items.push_back(select(
        "Palette", std::move(palettes),
        [this, palette_ids] {
          const std::string active =
              std::string(termusic::ui::normalizeVisualizerPaletteId(
                  context_->config().visualizer_palette));
          for (std::size_t index = 0; index < palette_ids.size(); ++index) {
            if (palette_ids[index] == active)
              return static_cast<int>(index);
          }
          return 0;
        },
        [this, palette_ids](int index) {
          if (palette_ids.empty())
            return;
          context_->controller.setVisualizerPalette(
              palette_ids[static_cast<std::size_t>(index) %
                          palette_ids.size()]);
          context_->visualizer_changed();
        },
        "The colour ramp the Spectrum draws in."));
    items.push_back(number(
        "Sensitivity",
        [this] {
          return static_cast<int>(context_->config().visualizer_sensitivity *
                                      10.0F +
                                  0.5F);
        },
        [this](int value) {
          context_->controller.setVisualizerSensitivity(
              static_cast<float>(value) / 10.0F);
          context_->visualizer_changed();
        },
        1, 50, 1, "\u00d7", 10, "How quickly the bars follow the music."));
    items.push_back(number(
        "Refresh", [this] { return context_->config().visualizer_refresh_hz; },
        [this](int value) {
          context_->controller.setVisualizerRefreshHz(value);
          context_->visualizer_changed();
        },
        5, 60, 1, " Hz", 1, "How often the spectrum is recomputed."));
    items.push_back(number(
        "Bands", [this] { return context_->config().visualizer_bar_density; },
        [this](int value) {
          context_->controller.setVisualizerDensity(value);
          context_->visualizer_changed();
        },
        32, 96, 1, " bands", 1,
        "How many frequency bands the analyzer produces."));
    list_.set(std::move(items));
  }
};

} // namespace

std::unique_ptr<SettingsSection> makeAppearanceSection() {
  return std::make_unique<AppearanceSection>();
}

} // namespace termusic::ui::core
