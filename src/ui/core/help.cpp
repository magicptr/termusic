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

    heading_row("Quick reference");
    line("j / k      move down / up in the focused list");
    line("h / l      move to the tree / move to the content pane");
    line("Enter      open, confirm, edit a setting, or play a track");
    line("g g / G    jump to the first / last row");
    line("PageDown / PageUp, or Ctrl+d / Ctrl+u in tracks, move a page");
    line("1 / 2      open Vault / Core");
    line("Space      play or pause");
    line("i          open or close Now Playing");
    line("Esc / q    close the current view / quit termusic");

    heading_row("MPD connection");
    line("MPD is the only playback backend. Switching backend means");
    line("connecting termusic to a different local or remote MPD server.");
    {
      const ConnectionSettings &effective = context.controller.connection();
      line("Current  " + effective.host + ":" +
           std::to_string(effective.port) + "  (" + effective.host_source +
           ")");
    }
    line("Local default: 127.0.0.1:6600.");
    line("Saved switch: press 2, open General with l, and move to Host.");
    line("Press Enter, type the new DNS name or IP, then press Enter again.");
    line("Edit Port and Password the same way when needed, then select Save");
    line("and reconnect and press Enter.");
    line("To return to the local backend, set Host to 127.0.0.1 and Port");
    line("to 6600, then run Save and reconnect again.");

    heading_row("Remote MPD");
    line("Set Host to the remote server's DNS name or IP address, Port to");
    line("its MPD port (normally 6600), and Password only if required.");
    line("For one run: termusic --host musicbox.lan --port 6600");
    line("Environment: MPD_HOST=musicbox.lan MPD_PORT=6600 termusic");
    line("Command line overrides environment, which overrides saved config.");
    line("The remote MPD must listen on a reachable address and allow your");
    line("client through its permissions and firewall. Music stays on the");
    line("MPD server; paths shown in Library belong to that server.");
    line("termusic never starts or configures the MPD daemon. If connection");
    line("fails, Core > General shows the endpoint and failure reason.");

    heading_row("Structure");
    line("Two roots. Vault holds music, Core holds settings.");
    line("Under Vault: Library is the media database, Playlists are saved");
    line("lists you own, History is what you played.");
    line("Under Core: General, Appearance, Keybindings, Plugins, About,");
    line("Help. The left column IS the list of settings modules.");

    heading_row("Moving and selecting");
    line("The highlighted pane owns the keys. Use h / l to cross between");
    line("the left tree and right track/settings pane, then j / k to move.");
    line("In a track list, v starts visual selection. Extend it with j / k;");
    line("y copies the selected tracks, while Esc cancels the selection.");

    heading_row("Playing");
    line("Enter on a track plays it and makes that collection the");
    line("playback context: the marker appears only where the run started.");
    line("Space toggles play/pause. In the immersive view h / l change");
    line("track and j / k change the volume. Sequential order follows the");
    line("collection you started from; Shuffle hands the order to MPD.");

    heading_row("Searching");
    line("/ opens the box at the bottom and filters the TRACK LIST of the");
    line("collection on screen: only the matching rows stay, and j / k (or");
    line("Up / Down) walk the results. Enter closes the box, restores the");
    line("full list and puts the cursor on the chosen track; Esc cancels.");
    line("n / N walk the accepted match in the full list afterwards.");

    heading_row("Playlist workflow");
    line("Create: in either Vault pane press a, type a name, then Enter.");
    line("Add one song: highlight it in the track pane and press y y.");
    line("Add many: press v, select with j / k, then press y.");
    line("Paste: press h, highlight the destination playlist with j / k,");
    line("then press p. The copied song or selection is appended to it.");
    line("Open a playlist and press l to edit its tracks: K / J moves the");
    line("highlighted song up / down, and d d removes it.");
    line("On a playlist in the tree, r renames it and d d deletes it.");
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
    line("MPD server settings live under Core > General.");
    for (SettingItem &item : items)
      item.label = item.label;
    list_.set(std::move(items));
    // Help is a long document, not a cyclic settings form. Keep the final line
    // visible when the user reaches it instead of wrapping back to the top.
    list_.setWrapNavigation(false);
  }
};

} // namespace

std::unique_ptr<SettingsSection> makeHelpSection() {
  return std::make_unique<HelpSection>();
}

} // namespace termusic::ui::core
