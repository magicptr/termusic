#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/actions.hpp"
#include "app/state.hpp"

namespace termusic {

/// The configuration schema termusic writes. A file that says nothing about the
/// schema is treated as version 1 (which is what every file written before the
/// key existed was).
inline constexpr int kConfigSchemaVersion = 1;

/// Built-in defaults for the MPD connection. They are also the values a user
/// gets on a machine where termusic has never run.
inline constexpr std::string_view kDefaultMpdHost = "127.0.0.1";
inline constexpr int kDefaultMpdPort = 6600;
/// Never zero: libmpdclient reads 0 as "wait forever", so an unresponsive
/// server would block every call -- including the stop() that quit performs --
/// and the process could not exit. Two seconds is far above a healthy round
/// trip and short enough that quitting always finishes.
inline constexpr int kDefaultMpdTimeoutMs = 2000;

/// ONE typed application configuration model.
///
/// Every subsystem reads and writes this struct and nothing else: Core panes
/// mutate it through the Controller, the Controller persists it through
/// ConfigStore, and no subsystem parses config.toml on its own. The
/// serialization format lives in this file too, so "what a key means" and "how
/// a key is spelled" cannot drift apart.
struct Config {
  /// Configuration file format version. Written so a future release can
  /// migrate; read so a file from a NEWER release is recognized instead of
  /// silently misread.
  int schema_version = kConfigSchemaVersion;

  Page start_page = Page::Library;
  /// Whether quitting also stops MPD playback. True keeps the historical
  /// behaviour: disconnecting the client alone would leave the daemon playing.
  bool stop_on_exit = true;
  int seek_step = 5;
  int volume_step = 5;

  std::string library_path;

  /// [keybindings.<context>] -> action id -> one or more key sequences.
  /// Only what the user actually writes is stored; everything else keeps its
  /// built-in default.
  std::map<std::string, std::map<std::string, std::vector<std::string>>>
      keybindings;

  // --- [mpd] ---------------------------------------------------------------
  // Empty host / port 0 mean "not configured here", so the environment can
  // still supply the endpoint; the effective values are resolved once, in
  // resolveConnection(), and never re-derived.
  std::string mpd_host = std::string(kDefaultMpdHost);
  int mpd_port = kDefaultMpdPort;
  std::string mpd_password;
  int mpd_timeout_ms = kDefaultMpdTimeoutMs;
  bool auto_reconnect = true;

  // --- [appearance] --------------------------------------------------------
  std::string theme_name = "default";
  /// Optional directory containing user themes (*.toml). Empty means the
  /// `themes` directory next to this configuration file.
  std::string theme_directory;
  /// "nerd" (default) or "unicode" for terminals without a Nerd Font.
  std::string icon_set = "nerd";
  /// "braille" (A), "half" (B) or "bg" (C).
  // Default is the verified renderer; "braille" is experimental and currently
  // renders blank, so it is opt-in only via --slider braille.
  std::string slider_renderer = "wave";
  /// Gaps between the five transport buttons, in cells.
  int transport_gap = 2;
  /// UI-only deterministic slider animation; never touches MPD.
  bool ui_slider_test = false;
  /// Show pointer/hit-test diagnostics in the hint bar.
  bool ui_mouse_debug = false;
  bool ui_visualizer_motion_test = false;
  /// A/B/C presentation preset for visual calibration.
  /// The active visualizer style: one of the ids in the style registry
  /// (`classic-bars`, `waterfall`, `particles`). A value the registry does not
  /// know -- a legacy "city", a typo -- is normalized to `classic-bars` on
  /// load, so a stored style can never fail to resolve.
  std::string visualizer_style = "classic-bars";
  /// The colour ramp, independent of the style: any style draws in any palette.
  std::string visualizer_palette = "theme";

  // --- [visualizer] --------------------------------------------------------
  bool visualizer_enabled = true;
  int visualizer_refresh_hz = 60;
  float visualizer_sensitivity = 1.0F;
  int visualizer_bar_density = 64;
  std::string visualizer_fifo = "/tmp/mpd.fifo";
  int visualizer_sample_rate = 44100;
  int visualizer_channels = 2;

  // --- [history] -----------------------------------------------------------
  /// Application-owned playback history. Disabling it stops termusic from
  /// reading or writing the file; the file itself is left alone.
  bool history_enabled = true;
  int history_max_entries = 100;

  // --- [plugins] -----------------------------------------------------------
  bool plugins_enabled = true;
  /// Optional directory containing native plugins (*.so / *.dylib). Empty
  /// means the `plugins` directory in termusic's XDG data directory.
  std::string plugin_directory;
  /// Plugin-owned values are preserved without the core knowing their schema.
  /// The outer key is the plugin id from a [plugin.<id>] section.
  std::map<std::string, std::map<std::string, std::string>> plugin_settings;

  /// Settings termusic does not model, kept exactly as written so that saving
  /// the file never drops a key belonging to a newer version, a plugin or a
  /// human editor. Comments are NOT preserved: the reader is line based and
  /// the writer regenerates the document.
  struct PreservedEntry {
    std::string section;
    std::string key;
    std::string value;
  };
  std::vector<PreservedEntry> preserved;
};

/// Converts Page to/from stable config values.
std::string_view pageId(Page page);
std::optional<Page> parsePage(std::string_view value);

/// The outcome of reading config.toml.
///
/// `warnings` are recoverable (a missing file, a migrated value, a value that
/// was replaced by a safe default) and are shown to the user. `errors` mean the
/// file asked for something invalid; `--check-config` fails on them, while the
/// TUI keeps running with the safe value.
struct ConfigLoad {
  Config config;
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
  bool ok() const { return errors.empty(); }
};

class ConfigStore {
public:
  explicit ConfigStore(std::filesystem::path path = defaultPath());

  static std::filesystem::path defaultPath();
  const std::filesystem::path &path() const { return path_; }

  /// True when the file exists and can be opened for reading.
  bool exists() const;

  /// Full result, with warnings and errors kept apart.
  ConfigLoad loadDetailed() const;

  /// Returns defaults when the file is missing or malformed. A non-empty
  /// warning describes recoverable parse/read errors.
  Config load(std::string *warning = nullptr) const;

  /// Writes the configuration ATOMICALLY: a temporary file in the same
  /// directory is written, flushed to disk and renamed over the real path, so a
  /// crash mid-save can never truncate an existing config.toml. A file that
  /// carries an MPD password is created with owner-only permissions.
  bool save(const Config &config, std::string *error = nullptr) const;

private:
  std::filesystem::path path_;
};

/// A complete, commented, valid configuration document -- the text
/// `--print-default-config` prints. It contains every key termusic reads, so
/// `termusic --config <file> --check-config` accepts it as written.
std::string defaultConfigText();

/// Values resolved for one connection attempt, with the origin of each one so
/// `--check-config` can show WHICH layer won.
struct ConnectionSettings {
  std::string host = std::string(kDefaultMpdHost);
  int port = kDefaultMpdPort;
  std::string password;
  int timeout_ms = kDefaultMpdTimeoutMs;
  bool auto_reconnect = true;

  /// "command line", "environment", "config.toml" or "default".
  std::string host_source = "default";
  std::string port_source = "default";
  std::string password_source = "default";
  std::string timeout_source = "default";
  std::string auto_reconnect_source = "default";
};

/// The process environment, read once. Passing it explicitly keeps the
/// precedence rules testable without touching the real environment.
struct Environment {
  std::string mpd_host;
  std::string mpd_port;
  std::string termusic_config;

  static Environment current();
};

/// What the command line asked for, in one place. A `*_set` flag distinguishes
/// "not given" from "given an empty/default value".
///
/// The connection fields take part in the documented precedence
/// (command line > environment > config.toml > defaults). The settings fields
/// have no environment layer: they simply override the file for this run and
/// are never written back, so a temporary flag cannot become permanent.
struct CliOverrides {
  // Connection.
  bool host_set = false;
  bool port_set = false;
  bool password_set = false;
  bool timeout_set = false;
  std::string host;
  int port = 0;
  std::string password;
  int timeout_ms = 0;

  /// `--config PATH`, the highest-precedence way to name the config file.
  std::string config_path;

  // Settings.
  bool fifo_set = false;
  std::string fifo;
  bool slider_set = false;
  std::string slider;
  bool gap_set = false;
  int gap = 0;
  bool icons_set = false;
  std::string icons;
  bool visualizer_style_set = false;
  std::string visualizer_style;
  bool visualizer_palette_set = false;
  std::string visualizer_palette;
  bool theme_directory_set = false;
  std::string theme_directory;
  bool plugin_directory_set = false;
  std::string plugin_directory;
  bool no_plugins = false;
  bool ui_slider_test = false;
  bool ui_mouse_debug = false;
  bool ui_visualizer_motion_test = false;
};

/// Canonical precedence: command line > environment > config.toml > defaults.
///
/// MPD_HOST follows the libmpdclient convention: an optional `password@` prefix
/// and an absolute path for a local socket are both honored, because that is
/// what every other MPD client on the machine accepts.
/// `config_file_present` distinguishes a value that came from the user's file
/// from one that is merely the built-in default (which is what an absent file
/// leaves in the model), so `--check-config` never claims the file said
/// something it did not.
ConnectionSettings resolveConnection(const Config &config,
                                     const CliOverrides &cli,
                                     const Environment &environment,
                                     bool config_file_present = true);

/// Finds duplicate non-contextual bindings for display in Settings.
std::vector<std::string>
keymapConflicts(const std::map<Action, std::string> &keymap);

/// Parses a `key = "value"` / `key = ["a", "b"]` right-hand side. Exposed for
/// the tests that pin the keybinding syntax.
std::vector<std::string> parseBindingList(const std::string &value);

} // namespace termusic
