#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "app/diagnostics.hpp"
#include "app/state.hpp"
#include "app/version.hpp"
#include "backend/mpd_backend.hpp"
#include "config/config.hpp"
#include "config/paths.hpp"
#include "controller/controller.hpp"
#include "extensions/extension_registry.hpp"
#include "extensions/plugin_manager.hpp"
#include "ui/app.hpp"
#include "ui/visualizer/palette.hpp"
#include "visualizer/analyzer.hpp"

namespace {

bool readValue(int argc, char **argv, int *index, std::string *value) {
  if (*index + 1 >= argc)
    return false;
  *value = argv[++(*index)];
  return true;
}

bool parseInteger(std::string_view value, int minimum, int maximum,
                  int *result) {
  int parsed = 0;
  const char *first = value.data();
  const char *last = first + value.size();
  const auto conversion = std::from_chars(first, last, parsed);
  if (conversion.ec != std::errc{} || conversion.ptr != last ||
      parsed < minimum || parsed > maximum)
    return false;
  *result = parsed;
  return true;
}

void printUsage() {
  std::cout
      << termusic::kVersionLine << "\n"
      << "A lightweight terminal music client. Playback is provided by MPD,\n"
         "either on this machine (the default) or on a remote server.\n"
         "\n"
         "Usage: termusic [options]\n"
         "\n"
         "Release options:\n"
         "  --help, -h              show this help and exit\n"
         "  --version               print the version and exit\n"
         "  --config PATH           read (and write) this configuration file\n"
         "                          instead of $XDG_CONFIG_HOME/termusic/\n"
         "                          config.toml\n"
         "  --check-config          validate the configuration, print a report\n"
         "                          and exit; never starts the UI or connects\n"
         "  --print-default-config  print a complete default configuration to\n"
         "                          stdout and exit\n"
         "\n"
         "MPD connection (command line > MPD_HOST/MPD_PORT > config > default):\n"
         "  --host HOST             server name, address or socket path\n"
         "  --port PORT             server port (default 6600)\n"
         "  --password VALUE        password; prefer config.toml or MPD_HOST,\n"
         "                          since command lines are visible in `ps`\n"
         "\n"
         "Other options:\n"
         "  --theme-dir PATH        directory of user themes (*.toml)\n"
         "  --plugin-dir PATH       directory of native plugins\n"
         "  --no-plugins            do not load plugins\n"
         "  --list-plugins          list discovered plugins and exit\n"
         "  --icons nerd|unicode    icon set for terminals without a Nerd Font\n"
         "  --fifo PATH             MPD spectrum fifo for the visualizer\n"
         "  --visualizer-palette ID theme | ice | fire | rainbow\n"
         "  --gap N                 gap between transport controls (0..4)\n"
         "  --slider braille|half|bg  slider renderer\n"
         "  --demo                  render a fixed reference state; never\n"
         "                          touches MPD\n"
         "\n"
         "Diagnostics:\n"
         "  --ui-script FILE        drive the UI from a script and assert on its\n"
         "                          own state and frames (test harness)\n"
         "  --ui-slider-test        animate the sliders from a synthetic track\n"
         "  --ui-mouse-debug        show pointer hit-test diagnostics\n"
         "  --ui-visualizer-motion-test  feed the visualizer a synthetic\n"
         "                          spectrum; never touches MPD\n"
         "\n"
         "termusic is a CLIENT: it never starts, stops, installs or configures\n"
         "the MPD daemon.\n";
}

/// The `--check-config` report. Never prints the password, never connects to
/// MPD, never writes anything.
int runCheckConfig(const termusic::AppPaths &paths,
                   const termusic::ConfigStore &store,
                   const termusic::ConnectionSettings &connection,
                   const termusic::ConfigLoad &load) {
  std::cout << "Config: ";
  if (paths.configFile().empty())
    std::cout << "(none: set HOME or XDG_CONFIG_HOME, or pass --config PATH)\n";
  else if (store.exists())
    std::cout << paths.configFile().string() << '\n';
  else
    std::cout << paths.configFile().string()
              << " (not found; built-in defaults apply)\n";
  std::cout << "Schema: " << load.config.schema_version << '\n';
  std::cout << "Status: " << (load.ok() ? "valid" : "invalid") << '\n';
  std::cout << "MPD: " << connection.host << ":" << connection.port << "  ["
            << connection.host_source << " / " << connection.port_source
            << "]\n";
  std::cout << "Password: "
            << (connection.password.empty() ? "not set" : "set (hidden)")
            << "  [" << connection.password_source << "]\n";
  std::cout << "Timeout: " << connection.timeout_ms << " ms\n";
  std::cout << "Auto reconnect: "
            << (connection.auto_reconnect ? "true" : "false") << '\n';
  std::cout << "History: "
            << (load.config.history_enabled ? "enabled" : "disabled") << ", up to "
            << load.config.history_max_entries << " entries\n";
  const auto directory = [](const std::filesystem::path &path) {
    return path.empty() ? std::string("(unresolved)") : path.string();
  };
  std::cout << "Data directory: " << directory(paths.data_directory) << '\n';
  std::cout << "Cache directory: " << directory(paths.cache_directory) << '\n';
  std::cout << "Plugin directory: "
            << (load.config.plugin_directory.empty()
                    ? directory(paths.pluginDirectory())
                    : load.config.plugin_directory)
            << '\n';
  if (!load.config.preserved.empty())
    std::cout << "Preserved settings: " << load.config.preserved.size()
              << " unknown key(s) kept for writing\n";
  for (const auto &warning : load.warnings)
    std::cout << "Warning: " << warning << '\n';
  for (const auto &error : load.errors)
    std::cout << "Error: " << error << '\n';
  return load.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char **argv) {
  // The command line is parsed FIRST, because --config/--help/--version decide
  // which file is even read. Nothing here touches the disk or the network.
  termusic::CliOverrides cli;
  bool demo = false;
  bool list_plugins = false;
  bool check_config = false;
  bool print_default_config = false;
  std::string ui_script;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    std::string value;
    if (argument == "--help" || argument == "-h") {
      printUsage();
      return EXIT_SUCCESS;
    }
    if (argument == "--version") {
      std::cout << termusic::kVersionLine << '\n';
      return EXIT_SUCCESS;
    }
    if (argument == "--check-config") {
      check_config = true;
    } else if (argument == "--print-default-config") {
      print_default_config = true;
    } else if (argument == "--config") {
      if (!readValue(argc, argv, &index, &value)) {
        std::cerr << "--config expects a path\n";
        return EXIT_FAILURE;
      }
      cli.config_path = std::move(value);
    } else if (argument == "--host") {
      if (!readValue(argc, argv, &index, &value)) {
        std::cerr << "--host expects a value\n";
        return EXIT_FAILURE;
      }
      cli.host = std::move(value);
      cli.host_set = true;
    } else if (argument == "--port") {
      if (!readValue(argc, argv, &index, &value)) {
        std::cerr << "--port expects a value\n";
        return EXIT_FAILURE;
      }
      if (!parseInteger(value, 1, 65535, &cli.port)) {
        std::cerr << "--port expects an integer from 1 to 65535\n";
        return EXIT_FAILURE;
      }
      cli.port_set = true;
    } else if (argument == "--password") {
      if (!readValue(argc, argv, &index, &value)) {
        std::cerr << "--password expects a value\n";
        return EXIT_FAILURE;
      }
      cli.password = std::move(value);
      cli.password_set = true;
    } else if (argument == "--fifo" && readValue(argc, argv, &index, &value)) {
      cli.fifo = std::move(value);
      cli.fifo_set = true;
    } else if (argument == "--demo") {
      demo = true;
    } else if (argument == "--slider" && readValue(argc, argv, &index, &value)) {
      cli.slider = std::move(value);
      cli.slider_set = true;
    } else if (argument == "--gap" && readValue(argc, argv, &index, &value)) {
      if (!parseInteger(value, 0, 4, &cli.gap)) {
        std::cerr << "--gap expects an integer from 0 to 4\n";
        return EXIT_FAILURE;
      }
      cli.gap_set = true;
    } else if (argument == "--ui-slider-test") {
      cli.ui_slider_test = true;
    } else if (argument == "--ui-mouse-debug") {
      cli.ui_mouse_debug = true;
    } else if (argument == "--ui-script" &&
               readValue(argc, argv, &index, &value)) {
      ui_script = std::move(value);
    } else if (argument == "--visualizer-palette" &&
               readValue(argc, argv, &index, &value)) {
      cli.visualizer_palette = std::string(
          termusic::ui::normalizeVisualizerPaletteId(value));
      cli.visualizer_palette_set = true;
    } else if (argument == "--ui-visualizer-motion-test") {
      cli.ui_visualizer_motion_test = true;
    } else if (argument == "--theme-dir" &&
               readValue(argc, argv, &index, &value)) {
      cli.theme_directory = std::move(value);
      cli.theme_directory_set = true;
    } else if (argument == "--plugin-dir" &&
               readValue(argc, argv, &index, &value)) {
      cli.plugin_directory = std::move(value);
      cli.plugin_directory_set = true;
    } else if (argument == "--no-plugins") {
      cli.no_plugins = true;
    } else if (argument == "--list-plugins") {
      list_plugins = true;
    } else if (argument == "--icons" && readValue(argc, argv, &index, &value)) {
      if (value != "nerd" && value != "unicode") {
        std::cerr << "--icons expects 'nerd' or 'unicode'\n";
        return EXIT_FAILURE;
      }
      cli.icons = std::move(value);
      cli.icons_set = true;
    } else {
      std::cerr << "Unknown or incomplete option: " << argument << '\n';
      printUsage();
      return EXIT_FAILURE;
    }
  }

  if (print_default_config) {
    // stdout only: no file is written, no server is contacted, no UI starts.
    std::cout << termusic::defaultConfigText();
    return EXIT_SUCCESS;
  }

  // 1. Load configuration: paths first (they decide WHICH file), then the file
  //    itself. A missing file is the normal first run, not an error.
  const termusic::AppPaths paths = termusic::resolveAppPaths(cli.config_path);
  termusic::ConfigStore config_store(paths.configFile());
  termusic::ConfigLoad load = config_store.loadDetailed();
  termusic::Config config = load.config;

  // 2. Resolve the effective MPD endpoint: command line > environment >
  //    config.toml > built-in defaults. The STORED config is deliberately not
  //    modified by the override, so saving an unrelated setting can never
  //    freeze a temporary `--host` into the user's file.
  const termusic::ConnectionSettings connection = termusic::resolveConnection(
      config, cli, termusic::Environment::current(), config_store.exists());

  if (check_config) {
    for (const auto &warning : load.warnings)
      std::cerr << "termusic: " << warning << '\n';
    return runCheckConfig(paths, config_store, connection, load);
  }

  // Command-line overrides that ARE settings (not connection endpoints) are
  // applied to the in-memory model only.
  if (cli.fifo_set)
    config.visualizer_fifo = cli.fifo;
  if (cli.slider_set)
    config.slider_renderer = cli.slider;
  if (cli.gap_set)
    config.transport_gap = cli.gap;
  if (cli.icons_set)
    config.icon_set = cli.icons;
  if (cli.visualizer_palette_set)
    config.visualizer_palette = cli.visualizer_palette;
  if (cli.theme_directory_set)
    config.theme_directory = cli.theme_directory;
  if (cli.plugin_directory_set) {
    config.plugin_directory = cli.plugin_directory;
    config.plugins_enabled = true;
  }
  if (cli.no_plugins)
    config.plugins_enabled = false;
  config.ui_slider_test = cli.ui_slider_test;
  config.ui_mouse_debug = cli.ui_mouse_debug;
  config.ui_visualizer_motion_test = cli.ui_visualizer_motion_test;

  // 3. Application-owned directories. Nothing is created here: a directory
  //    appears only when something is actually written into it.
  const std::filesystem::path theme_directory =
      config.theme_directory.empty() ? paths.themeDirectory()
                                     : std::filesystem::path(config.theme_directory);
  const std::filesystem::path plugin_directory =
      config.plugin_directory.empty() ? paths.pluginDirectory()
                                      : std::filesystem::path(config.plugin_directory);

  // Diagnostics: configuration problems and connection failures are worth
  // keeping. The log lives in the XDG cache directory, is written only when
  // there is something to say, and never carries a password.
  termusic::DiagnosticsLog diagnostics(paths.logFile());
  for (const auto &warning : load.warnings)
    diagnostics.write("config: " + warning);
  for (const auto &error : load.errors)
    diagnostics.write("config error: " + error);
  if (!paths.configFile().empty() && !config_store.exists())
    diagnostics.write("config: " + paths.configFile().string() +
                      " does not exist; built-in defaults in use");

  termusic::ui::ThemeRegistry themes;
  std::vector<std::string> extension_warnings;
  if (!theme_directory.empty())
    themes.loadDirectory(theme_directory, &extension_warnings);
  termusic::extensions::ExtensionRegistry extensions;
  termusic::extensions::PluginManager plugins(extensions, themes,
                                              config.plugin_settings);
  if (config.plugins_enabled && !plugin_directory.empty())
    plugins.loadDirectory(plugin_directory);
  extension_warnings.insert(extension_warnings.end(),
                            plugins.warnings().begin(),
                            plugins.warnings().end());
  if (list_plugins) {
    for (const auto &plugin : plugins.plugins())
      std::cout << plugin.id << '\t' << plugin.version << '\t' << plugin.path
                << '\n';
    for (const auto &warning : extension_warnings)
      std::cerr << "warning: " << warning << '\n';
    return extension_warnings.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  termusic::AppState state;
  state.demo = demo;
  if (!load.warnings.empty()) {
    state.toast = load.warnings.front();
    state.toast_at = std::chrono::steady_clock::now();
  } else if (!load.errors.empty()) {
    state.toast = load.errors.front();
    state.toast_at = std::chrono::steady_clock::now();
  }
  if (!extension_warnings.empty()) {
    for (const auto &warning : extension_warnings) {
      std::cerr << "termusic: " << warning << '\n';
      diagnostics.write("extension: " + warning);
    }
    if (!state.toast) {
      state.toast = extension_warnings.front();
      state.toast_at = std::chrono::steady_clock::now();
    }
  }
  termusic::MpdBackend backend;
  termusic::Controller controller(backend, state, config, config_store);
  // The endpoint to actually use, resolved once above. Never persisted, so a
  // temporary override cannot leak into config.toml.
  controller.useConnectionSettings(connection);
  controller.setDiagnosticsLog(&diagnostics);
  termusic::VisualizerAnalyzer analyzer;
  termusic::ui::Application application(state, controller, backend, analyzer,
                                        themes, extensions);
  if (!ui_script.empty()) {
    std::ifstream script(ui_script);
    if (!script) {
      std::cerr << "cannot read ui script: " << ui_script << '\n';
      return EXIT_FAILURE;
    }
    const int failures =
        application.runScript(script, std::cout);
    application.stopForScript();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  return application.run();
}
