#include "app/version.hpp"
#include "ui/core/core.hpp"
#include "ui/core/settings.hpp"

#include <string>
#include <vector>

namespace termusic::ui::core {
namespace {

/// About: what this program is, what it is licensed under, and what it is
/// built from. Read-only, like Help -- there is nothing to configure here.
class AboutSection final : public SettingsListSection {
public:
  std::string_view title() const override { return "About"; }

protected:
  void fill(const CoreContext &context) override {
    (void)context;
    std::vector<SettingItem> items;
    const auto line = [&items](std::string text) {
      items.push_back(note(std::move(text)));
    };

    items.push_back(heading("termusic"));
    line(std::string(kVersionLine));
    line("A terminal music client for MPD: the daemon owns playback, the");
    line("decoder, the output, the database and the queue.");

    items.push_back(heading("License"));
    line("GNU General Public License, version 3 or later.");
    line("Third-party notices: THIRD_PARTY_LICENSES.md");

    items.push_back(heading("Bundled"));
    line("FTXUI 7.0.3 (MIT)             terminal interface");
    line("libmpdclient 2.26 (BSD)       MPD client library");
    line("kissfft 131.2.0 (BSD-3-Clause) spectrum analysis");
    line("All three are linked statically into this binary.");

    list_.set(std::move(items));
  }
};

} // namespace

std::unique_ptr<SettingsSection> makeAboutSection() {
  return std::make_unique<AboutSection>();
}

} // namespace termusic::ui::core
