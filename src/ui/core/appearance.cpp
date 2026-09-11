#include "ui/core/core.hpp"
#include "ui/core/settings.hpp"
#include "ui/visualizer/palette.hpp"
#include "ui/visualizer/renderer.hpp"

#include <string>
#include <vector>

namespace termusic::ui::core {
namespace {

/// Appearance: how the application looks, and how the visualizer listens.
///
/// The theme, the palette and the four visualizer controls used to be
/// hand-built sliders and buttons; they are setting items now, so this file
/// contains no rendering code at all.
class AppearanceSection final : public SettingsListSection {
public:
  std::string_view title() const override { return "Appearance"; }

protected:
  void fill(const CoreContext &context) override {
    std::vector<std::string> themes;
    themes.reserve(context.themes.list().size());
    for (const ThemeInfo &info : context.themes.list())
      themes.push_back(info.id);

    // Style and Palette are separate choices on purpose: any style draws in
    // any palette. Both lists come from their registries, so a new style or a
    // new ramp appears here without touching this file.
    std::vector<std::string> styles;
    std::vector<std::string> style_ids;
    for (const termusic::ui::VisualizerStyle &style :
         termusic::ui::visualizerStyles()) {
      styles.emplace_back(style.label);
      style_ids.emplace_back(style.id);
    }
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
        "Catppuccin Mocha is the default; Nord ships with it too."));
    items.push_back(heading("Visualizer"));
    items.push_back(select(
        "Style", std::move(styles),
        [this, style_ids] {
          const std::string active =
              std::string(termusic::ui::normalizeVisualizerStyleId(
                  context_->config().visualizer_style));
          for (std::size_t index = 0; index < style_ids.size(); ++index) {
            if (style_ids[index] == active)
              return static_cast<int>(index);
          }
          return 0;
        },
        [this, style_ids](int index) {
          if (style_ids.empty())
            return;
          context_->controller.setVisualizerStyle(
              style_ids[static_cast<std::size_t>(index) % style_ids.size()]);
          // The Application owns the live renderer: it rebuilds and resets it.
          context_->visualizer_changed();
        },
        "Classic Bars, Waterfall or Particles. Switching is live."));
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
        "The colour ramp, independent of the style."));
    items.push_back(toggle(
        "Enabled", [this] { return context_->config().visualizer_enabled; },
        [this](bool value) {
          context_->controller.setVisualizerEnabled(value);
          context_->visualizer_changed();
        },
        "Draws the spectrum in the immersive now-playing view."));
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
