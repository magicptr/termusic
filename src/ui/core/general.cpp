#include "ui/core/core.hpp"
#include "ui/core/settings.hpp"

#include <string>
#include <vector>

namespace termusic::ui::core {
namespace {

/// General: where the application starts, and how far one key press moves.
///
/// A handful of settings, one item descriptor each. There is no widget code
/// here: the shared list engine owns navigation, editing and drawing.
class GeneralSection final : public SettingsListSection {
public:
  std::string_view title() const override { return "General"; }

protected:
  void fill(const CoreContext &context) override {
    context_ = &context;
    std::vector<std::string> pages;
    pages.reserve(allPages().size());
    for (const Page page : allPages())
      pages.emplace_back(pageLabel(page));

    std::vector<SettingItem> items;
    items.push_back(heading("Startup"));
    items.push_back(select(
        "Startup page", std::move(pages),
        [this] { return context_->start_page_index(); },
        [this](int index) { context_->set_start_page_index(index); },
        "The section shown when termusic starts."));
    items.push_back(toggle(
        "Stop MPD on exit", [this] { return context_->config().stop_on_exit; },
        [this](bool value) { context_->controller.setStopOnExit(value); },
        "Off: quitting only disconnects and MPD keeps playing."));
    items.push_back(heading("Steps"));
    items.push_back(number(
        "Seek step", [this] { return context_->config().seek_step; },
        [this](int value) { context_->controller.setSeekStep(value); }, 1, 60,
        1, " s", 1, "How far one seek key moves."));
    items.push_back(number(
        "Volume step", [this] { return context_->config().volume_step; },
        [this](int value) { context_->controller.setVolumeStep(value); }, 1, 25,
        1, " %", 1, "How far one volume key moves."));
    list_.set(std::move(items));
  }
};

} // namespace

std::unique_ptr<SettingsSection> makeGeneralSection() {
  return std::make_unique<GeneralSection>();
}

} // namespace termusic::ui::core
