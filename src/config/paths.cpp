#include "config/paths.hpp"

#include <cstdlib>

namespace termusic {
namespace {

/// The value of an environment variable, or empty when unset/blank.
std::string envValue(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string{};
}

/// An XDG base directory, or the fallback under `$HOME`.
///
/// The specification says a relative path must be ignored and an empty value
/// treated as unset; both rules are applied here so a stray `XDG_DATA_HOME=.`
/// can never make termusic write into the working directory.
std::filesystem::path baseDirectory(const char *variable,
                                    const std::filesystem::path &home,
                                    const char *fallback) {
  const std::string value = envValue(variable);
  if (!value.empty()) {
    const std::filesystem::path candidate(value);
    if (candidate.is_absolute())
      return candidate;
  }
  if (home.empty())
    return {};
  return home / fallback;
}

} // namespace

std::string homeDirectory() { return envValue("HOME"); }

AppPaths resolveAppPaths() { return resolveAppPaths({}); }

AppPaths resolveAppPaths(const std::string &cli_config) {
  const std::filesystem::path home = homeDirectory();

  // Every termusic path is `<base>/termusic`: one directory per base type, so
  // termusic can be removed by deleting four directories and can never collide
  // with another application's files.
  const auto beneathTermusic = [](const std::filesystem::path &base) {
    return base.empty() ? base : base / "termusic";
  };
  AppPaths paths;
  paths.config_directory =
      beneathTermusic(baseDirectory("XDG_CONFIG_HOME", home, ".config"));
  paths.data_directory =
      beneathTermusic(baseDirectory("XDG_DATA_HOME", home, ".local/share"));
  paths.cache_directory =
      beneathTermusic(baseDirectory("XDG_CACHE_HOME", home, ".cache"));
  paths.state_directory =
      beneathTermusic(baseDirectory("XDG_STATE_HOME", home, ".local/state"));

  // The config file: `--config` beats TERMUSIC_CONFIG beats the XDG location.
  // All three are resolved HERE, so every consumer sees one path and no
  // subsystem ever re-derives it.
  std::filesystem::path explicit_file;
  if (!cli_config.empty()) {
    explicit_file = cli_config;
  } else {
    const std::string env_config = envValue("TERMUSIC_CONFIG");
    if (!env_config.empty())
      explicit_file = env_config;
  }
  if (!explicit_file.empty()) {
    // A bare file name (or a relative one) is used as given: it is what the
    // user typed, and the working directory is then their explicit choice
    // rather than something termusic invented.
    paths.config_file = explicit_file;
    paths.config_directory = explicit_file.parent_path();
    if (paths.config_directory.empty())
      paths.config_directory = ".";
  } else if (!paths.config_directory.empty()) {
    paths.config_file = paths.config_directory / "config.toml";
  } else {
    // No HOME and no XDG_CONFIG_HOME: there is no correct place to write, and
    // inventing one (the working directory, say) is exactly what the XDG
    // convention forbids. The empty path means "no file": loading falls back to
    // the built-in defaults, saving reports that it cannot determine a path.
    paths.config_file.clear();
    paths.config_directory.clear();
  }
  return paths;
}

std::filesystem::path AppPaths::historyFile() const {
  if (data_directory.empty())
    return {};
  return data_directory / "history.toml";
}

std::filesystem::path AppPaths::themeDirectory() const {
  if (config_directory.empty())
    return {};
  return config_directory / "themes";
}

std::filesystem::path AppPaths::logFile() const {
  if (cache_directory.empty())
    return {};
  return cache_directory / "termusic.log";
}

} // namespace termusic
