#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace termusic {

/// The XDG Base Directory layout termusic uses, resolved once.
///
/// Every path termusic owns (config, data, cache, state) comes from here, so
/// there is exactly one place that knows how the base directories are found and
/// exactly one place to change if that ever needs to differ. Nothing in the
/// program writes into the working directory, the source tree, `$HOME` itself
/// or any MPD-owned directory.
///
/// Resolution follows the XDG Base Directory Specification:
///
///   config: $XDG_CONFIG_HOME/termusic      -> ~/.config/termusic
///   data:   $XDG_DATA_HOME/termusic        -> ~/.local/share/termusic
///   cache:  $XDG_CACHE_HOME/termusic       -> ~/.cache/termusic
///   state:  $XDG_STATE_HOME/termusic       -> ~/.local/state/termusic
///
/// A relative or empty XDG value is ignored, as the specification requires.
struct AppPaths {
  std::filesystem::path config_directory;
  std::filesystem::path data_directory;
  std::filesystem::path cache_directory;
  /// Only used to find data written by an older termusic (history used to live
  /// under XDG_STATE_HOME). Nothing is written here.
  std::filesystem::path state_directory;

  /// The user's config file, unless `--config` (or TERMUSIC_CONFIG) named one.
  std::filesystem::path config_file;

  std::filesystem::path configFile() const { return config_file; }
  std::filesystem::path historyFile() const;
  std::filesystem::path pluginDirectory() const;
  std::filesystem::path themeDirectory() const;
  std::filesystem::path logFile() const;
};

/// Directories only: no file and no environment override for the config path.
AppPaths resolveAppPaths();

/// Reads TERMUSIC_CONFIG, then `--config PATH` on top of it. `cli_config`
/// wins; an empty value means "not given". The returned paths are NOT created:
/// termusic only creates a directory when it is about to write into it, so a
/// fresh install leaves no empty tree behind.
AppPaths resolveAppPaths(const std::string &cli_config);

/// `$HOME` as the shell would see it, or empty when unset. Exposed because the
/// config walk needs the same notion of "no home directory" as the paths above.
std::string homeDirectory();

} // namespace termusic
