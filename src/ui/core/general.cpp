#include "ui/core/core.hpp"
#include "ui/core/settings.hpp"

#include <string>
#include <utility>
#include <vector>

namespace termusic::ui::core {
namespace {

/// General: what the application starts on, how far one key press moves, and
/// which MPD server it is a client of.
///
/// The MPD server settings live here rather than in a section of their own:
/// they are the first thing a new user changes, and the tree keeps frequently
/// used configuration at the top. Everything is expressed as setting items --
/// there is no widget code here; the shared list engine owns navigation,
/// editing and drawing.
class GeneralSection final : public SettingsListSection {
public:
  std::string_view title() const override { return "General"; }

protected:
  void fill(const CoreContext &context) override {
    context_ = &context;
    // The fields edit LOCAL text; nothing is applied until "Save and
    // reconnect" runs, so a half-typed host name never reaches the connection.
    host_ = context.config().mpd_host;
    port_ = context.config().mpd_port;
    password_ = context.config().mpd_password;

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

    items.push_back(heading("MPD server"));
    items.push_back(note([this] {
      // A line that reads the live connection state, refreshed every frame.
      const bool connected =
          context_ != nullptr && context_->state.mpd_connected;
      return std::string("Server  ") +
             (connected ? "connected" : "disconnected");
    }));
    items.push_back(note([this] {
      // Why it is disconnected, in one short phrase the backend produced
      // ("Connection refused", "Authentication failed", "Timed out"). Never a
      // raw implementation dump and never a password.
      if (context_ == nullptr || context_->state.mpd_connected)
        return std::string();
      // A std::string, NOT a string_view of one: a view of the ternary's
      // temporary dies at the end of the expression, and the row then renders
      // as fragments of whatever that memory is reused for.
      const std::string reason = context_->state.error.value_or(std::string());
      if (reason.empty())
        return std::string("Reason  not connected yet");
      return "Reason  " + reason;
    }));
    items.push_back(note([this] {
      // The endpoint actually in use, after the command line and the
      // environment were applied: what the pane shows as the SAVED value may
      // be overridden for this run, and hiding that would be a lie.
      if (context_ == nullptr)
        return std::string();
      const ConnectionSettings &effective = context_->controller.connection();
      return "Using   " + effective.host + ":" +
             std::to_string(effective.port) + "  (" + effective.host_source +
             ")";
    }));
    items.push_back(input(
        "Host", [this] { return host_; },
        [this](const std::string &value) { host_ = value; },
        "Enter, type the replacement host or IP, then Enter again. Empty uses "
        "MPD_HOST, then the default host."));
    items.push_back(number("Port", [this] { return port_; },
                           [this](int value) { port_ = value; }, 0, 65535, 1,
                           std::string(), 1, "0 uses MPD_PORT, then 6600."));
    items.push_back(input(
        "Password", [this] { return password_; },
        [this](const std::string &value) { password_ = value; },
        "Enter and type a replacement. Leave empty when the server requires "
        "no password.", true));
    items.push_back(toggle(
        "Auto reconnect", [this] { return context_->config().auto_reconnect; },
        [this](bool value) {
          context_->controller.config().auto_reconnect = value;
          context_->save_config();
        },
        "Reconnect on its own when the server goes away."));
    items.push_back(heading("Actions"));
    items.push_back(action(
        "Save and reconnect",
        [this] { context_->connection_changed(host_, port_, password_); },
        "Applies the fields above and reconnects."));
    items.push_back(action(
        "Update database", [this] { context_->update_database(); },
        "Asks MPD to rescan its own music_directory."));
    items.push_back(note(
        "termusic is a client: it never starts, stops or configures the MPD "
        "daemon."));
    list_.set(std::move(items));
  }

private:
  std::string host_;
  int port_ = 0;
  std::string password_;
};

} // namespace

std::unique_ptr<SettingsSection> makeGeneralSection() {
  return std::make_unique<GeneralSection>();
}

} // namespace termusic::ui::core
