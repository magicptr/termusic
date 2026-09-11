#include "config/paths.hpp"
#include "ui/core/core.hpp"
#include "ui/core/settings.hpp"

#include <string>
#include <vector>

namespace termusic::ui::core {
namespace {

using namespace ftxui;

/// Help: how to use the application, as scrollable text.
///
/// It owns no configuration and no widgets beyond the shared setting list, so
/// there is exactly one place that explains the model -- and it is the same
/// list engine every other module uses, which is what gives it scrolling.
class HelpSection final : public SettingsListSection {
public:
  std::string_view title() const override { return "Help"; }

protected:
  void fill(const CoreContext &context) override {
    const Config &config = context.config();
    std::vector<SettingItem> items;

    const auto heading_row = [&items](const char *text) {
      items.push_back(heading(text));
    };
    const auto line = [&items](std::string text) {
      items.push_back(note(std::move(text)));
    };

    heading_row("Backend");
    line("termusic uses MPD for playback and never starts, stops or");
    line("configures it: the MPD daemon is owned by you or your system.");
    {
      const ConnectionSettings &effective = context.controller.connection();
      line("Endpoint  " + effective.host + ":" +
           std::to_string(effective.port) + "  (" + effective.host_source +
           ")");
    }
    line("MPD may run on this machine or on a remote server. The default is");
    line("127.0.0.1:6600; override it with MPD_HOST/MPD_PORT, --host/--port,");
    line("or Core > Connection (which also saves it).");
    line("No MPD running? termusic still opens: Core > Connection explains");
    line("what failed and lets you point it somewhere else.");

    heading_row("Structure");
    line("Two roots. Vault holds music, Core holds settings.");
    line("Under Vault: Library is the media database, Playlists are saved");
    line("lists you own, History is what you played.");
    line("Under Core: Help, Connection, General, Appearance, Keybindings,");
    line("Plugins. The left column IS the list of settings modules.");

    heading_row("Getting around");
    line("j / k      move in the tree, and in the open settings pane");
    line("h / l      leave the pane / enter it");
    line("Enter      open a collection, a settings module, or a setting");
    line("g g / G    first / last row");
    line("1 / 2      jump to the Vault root / the Core root");

    heading_row("Playing");
    line("Enter on a track plays it and makes that collection the");
    line("playback context: the marker appears only where the run started.");
    line("Space toggles play/pause. In the immersive view h / l change");
    line("track and j / k change the volume. Sequential order follows the");
    line("collection you started from; Shuffle hands the order to MPD.");

    heading_row("Searching");
    line("/ opens the search box at the bottom and searches what the");
    line("focused pane shows (the tree, or the track list). The cursor");
    line("follows what you type. Enter keeps the match, Esc puts the");
    line("cursor back. n / N walk the accepted match in the track list.");

    heading_row("Playlists");
    line("a          create a playlist");
    line("r          rename the highlighted playlist");
    line("d d        delete the highlighted playlist");
    line("p          paste the register into the highlighted playlist");
    line("Every listed playlist is a real saved playlist you own; none of");
    line("them is reserved, and MPD's runtime queue is not a collection.");

    heading_row("Leaving");
    line("q          always quits, from any pane, whatever the config says.");
    line("Esc        closes what is open; the second Esc returns to the tree.");

    heading_row("Files");
    line("config  " + context.config_path());
    {
      // One resolver for every termusic-owned path, called rather than
      // re-derived: Help is describing the same layout the program uses.
      const AppPaths paths = resolveAppPaths();
      line("data    " + (paths.data_directory.empty()
                             ? std::string("(no HOME or XDG_DATA_HOME)")
                             : paths.data_directory.string()));
      line("logs    " + (paths.logFile().empty()
                             ? std::string("(unavailable)")
                             : paths.logFile().string()));
    }
    line("plugins " + (config.plugin_directory.empty()
                         ? std::string("(the termusic data directory)")
                         : config.plugin_directory));
    line("Drop a native plugin there and list it under Core > Plugins.");
    line("MPD server settings live under Core > Connection.");
    for (SettingItem &item : items)
      item.label = item.label;
    list_.set(std::move(items));
  }
};

} // namespace

std::unique_ptr<SettingsSection> makeHelpSection() {
  return std::make_unique<HelpSection>();
}

} // namespace termusic::ui::core
