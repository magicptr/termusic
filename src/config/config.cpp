#include "config/config.hpp"

#include "config/paths.hpp"
#include "ui/visualizer/palette.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace termusic {
namespace {

std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::string stripComment(std::string_view line) {
  bool in_quotes = false;
  bool escaped = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char character = line[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (in_quotes && character == '\\') {
      escaped = true;
      continue;
    }
    if (character == '"') {
      in_quotes = !in_quotes;
      continue;
    }
    if (character == '#' && !in_quotes)
      return std::string(line.substr(0, index));
  }
  return std::string(line);
}

std::string unquote(std::string_view value) {
  std::string result = trim(value);
  if (result.size() < 2 || result.front() != '"' || result.back() != '"')
    return result;

  std::string decoded;
  decoded.reserve(result.size() - 2);
  bool escaped = false;
  for (std::size_t index = 1; index + 1 < result.size(); ++index) {
    const char character = result[index];
    if (!escaped && character == '\\') {
      escaped = true;
      continue;
    }
    if (escaped) {
      switch (character) {
      case 'n':
        decoded.push_back('\n');
        break;
      case 'r':
        decoded.push_back('\r');
        break;
      case 't':
        decoded.push_back('\t');
        break;
      default:
        decoded.push_back(character);
        break;
      }
      escaped = false;
      continue;
    }
    decoded.push_back(character);
  }
  if (escaped)
    decoded.push_back('\\');
  return decoded;
}

std::string quote(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('"');
  for (const char c : value) {
    if (c == '\\' || c == '"') {
      result.push_back('\\');
      result.push_back(c);
    } else if (c == '\n') {
      result += "\\n";
    } else if (c == '\r') {
      result += "\\r";
    } else if (c == '\t') {
      result += "\\t";
    } else {
      result.push_back(c);
    }
  }
  result.push_back('"');
  return result;
}

bool parseBool(std::string_view value, bool *target) {
  const std::string text = trim(value);
  if (text == "true") {
    *target = true;
    return true;
  }
  if (text == "false") {
    *target = false;
    return true;
  }
  return false;
}

template <typename T> bool parseNumber(std::string_view value, T *target) {
  std::istringstream input{std::string(value)};
  T parsed{};
  input >> parsed;
  if (!input || !input.eof())
    return false;
  *target = parsed;
  return true;
}

bool allowedContextDuplicate(Action lhs, Action rhs) {
  const auto is_pair = [lhs, rhs](Action a, Action b) {
    return (lhs == a && rhs == b) || (lhs == b && rhs == a);
  };
  return is_pair(Action::SeekBackward, Action::GoParent) ||
         is_pair(Action::SeekForward, Action::EnterDirectory) ||
         is_pair(Action::Activate, Action::Confirm);
}

/// Collects the read errors and warnings without deciding what to do about
/// them: `--check-config` fails on the first vector, the TUI only shows both.
struct Diagnostics {
  std::vector<std::string> *warnings = nullptr;
  std::vector<std::string> *errors = nullptr;

  void warn(std::string message) {
    if (warnings != nullptr)
      warnings->push_back(std::move(message));
  }
  /// Records an invalid value AND the safe replacement that was used, so the
  /// user is never left guessing what the program is running with.
  void invalid(const std::string &section, const std::string &key,
               const std::string &detail, const std::string &fallback) {
    if (errors != nullptr)
      errors->push_back("[" + section + "] " + key + ": " + detail +
                        "; using " + fallback);
  }
};

/// One numeric field: parsed, range-checked and replaced by its default when it
/// is nonsense. Nothing is silently clamped -- an out-of-range value is
/// reported, which is what `--check-config` needs to return non-zero.
template <typename T>
bool readNumber(const std::string &raw, T low, T high, T fallback, T *target,
                Diagnostics &diagnostics, const char *section,
                const char *key) {
  T parsed{};
  if (!parseNumber(raw, &parsed)) {
    diagnostics.invalid(section, key, "\"" + raw + "\" is not a number",
                        std::to_string(fallback));
    *target = fallback;
    return false;
  }
  if (parsed < low || parsed > high) {
    std::ostringstream range;
    range << "must be between " << low << " and " << high << ", got " << parsed;
    diagnostics.invalid(section, key, range.str(), std::to_string(fallback));
    *target = fallback;
    return false;
  }
  *target = parsed;
  return true;
}

bool readBool(const std::string &raw, bool fallback, bool *target,
              Diagnostics &diagnostics, const char *section, const char *key) {
  if (!parseBool(raw, target)) {
    diagnostics.invalid(section, key, "\"" + raw + "\" is not true or false",
                        fallback ? "true" : "false");
    *target = fallback;
    return false;
  }
  return true;
}

bool readString(const std::string &raw, std::string *target) {
  *target = unquote(raw);
  return true;
}

bool keyIsKnown(const std::string &section, const std::string &key) {
  static const std::unordered_map<std::string, std::unordered_set<std::string>>
      keys = {
          // Top level (no section header) holds only the schema marker.
          {"", {"schema_version"}},
          {"general", {"start_page", "stop_on_exit", "seek_step", "volume_step"}},
          {"library", {"path"}},
          {"mpd",
           {"host", "port", "password", "timeout_ms", "auto_reconnect"}},
          {"appearance",
           {"theme", "theme_directory", "icons", "slider", "transport_gap"}},
          {"theme", {"name", "directory"}},
          {"visualizer",
           {"enabled", "style", "palette", "refresh_hz", "sensitivity",
            "bar_density", "fifo_path", "sample_rate", "channels"}},
          {"history", {"enabled", "max_entries"}},
      };
  const auto dot = section.find('.');
  const std::string head =
      dot == std::string::npos ? section : section.substr(0, dot);
  if (head == "keybindings")
    return true;
  const auto found = keys.find(head);
  return found != keys.end() && found->second.count(key) != 0;
}

/// Parses a document into `load`. Shared by the file reader and by the tests
/// that feed `defaultConfigText()` straight back in.
void parseDocument(std::istream &input, ConfigLoad *load) {
  Config &config = load->config;
  Diagnostics diagnostics{&load->warnings, &load->errors};

  std::string section;
  std::string line;
  bool schema_seen = false;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    line = stripComment(line);
    line = trim(line);
    if (line.empty())
      continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(std::string_view(line).substr(1, line.size() - 2));
      continue;
    }
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      // The line itself is NOT echoed: a malformed line may hold a password,
      // and this message can end up in a log.
      diagnostics.warn("ignored line " + std::to_string(line_number) +
                       ": it is not `key = value`");
      continue;
    }
    const std::string key = trim(std::string_view(line).substr(0, separator));
    const std::string value =
        trim(std::string_view(line).substr(separator + 1));

    if (!keyIsKnown(section, key)) {
      // Not termusic's: keep it exactly as written so a save cannot drop it.
      config.preserved.push_back({section, key, value});
      continue;
    }

    if (section == "general") {
      if (key == "start_page") {
        const auto page = parsePage(unquote(value));
        if (page)
          config.start_page = *page;
        else
          diagnostics.invalid("general", "start_page", "\"" + unquote(value) +
                                               "\" is not a page",
                              "\"" + std::string(pageId(config.start_page)) +
                                  "\"");
      } else if (key == "stop_on_exit") {
        readBool(value, true, &config.stop_on_exit, diagnostics, "general",
                 "stop_on_exit");
      } else if (key == "seek_step") {
        readNumber(value, 1, 60, 5, &config.seek_step, diagnostics, "general",
                   "seek_step");
      } else if (key == "volume_step") {
        readNumber(value, 1, 25, 5, &config.volume_step, diagnostics, "general",
                   "volume_step");
      }
    } else if (section.rfind("keybindings.", 0) == 0) {
      const std::string context =
          section.substr(std::string("keybindings.").size());
      std::vector<std::string> sequences = parseBindingList(value);
      if (sequences.empty())
        diagnostics.invalid(section, key, "\"" + value + "\" is not a key list",
                            "the built-in binding");
      else
        config.keybindings[context][key] = std::move(sequences);
    } else if (section == "library" && key == "path") {
      readString(value, &config.library_path);
    } else if (section == "mpd") {
      if (key == "host")
        readString(value, &config.mpd_host);
      else if (key == "port")
        readNumber(value, 0, 65535, kDefaultMpdPort, &config.mpd_port,
                   diagnostics, "mpd", "port");
      else if (key == "password")
        readString(value, &config.mpd_password);
      else if (key == "timeout_ms")
        readNumber(value, 100, 60000, kDefaultMpdTimeoutMs,
                   &config.mpd_timeout_ms, diagnostics, "mpd", "timeout_ms");
      else if (key == "auto_reconnect")
        readBool(value, true, &config.auto_reconnect, diagnostics, "mpd",
                 "auto_reconnect");
    } else if (section == "appearance" || section == "theme") {
      if (key == "theme" || key == "name")
        readString(value, &config.theme_name);
      else if (key == "theme_directory" || key == "directory")
        readString(value, &config.theme_directory);
      else if (key == "icons") {
        const std::string icons = unquote(value);
        if (icons == "nerd" || icons == "unicode")
          config.icon_set = icons;
        else
          diagnostics.invalid(section, "icons",
                              "\"" + icons + "\" is not nerd or unicode",
                              "\"nerd\"");
      } else if (key == "slider") {
        readString(value, &config.slider_renderer);
      } else if (key == "transport_gap") {
        readNumber(value, 0, 4, 2, &config.transport_gap, diagnostics,
                   "appearance", "transport_gap");
      }
    } else if (section == "history") {
      if (key == "enabled")
        readBool(value, true, &config.history_enabled, diagnostics, "history",
                 "enabled");
      else if (key == "max_entries")
        readNumber(value, 1, 10000, 100, &config.history_max_entries,
                   diagnostics, "history", "max_entries");
    } else if (section.empty() && key == "schema_version") {
      int version = kConfigSchemaVersion;
      if (readNumber(value, 1, 1000, kConfigSchemaVersion, &version,
                     diagnostics, "termusic", "schema_version")) {
        config.schema_version = version;
        schema_seen = true;
        if (version > kConfigSchemaVersion)
          diagnostics.warn(
              "config.toml was written by a newer termusic (schema " +
              std::to_string(version) + "); unknown settings are preserved");
      }
    } else if (section == "visualizer") {
      if (key == "style" || key == "enabled") {
        // LEGACY, ignored on purpose -- and silently: there is ONE visualizer
        // now (the Spectrum), so a stored style has nothing to select, and a
        // stored `enabled = false` cannot leave the display blank while
        // Appearance says Visualizer is on. Both keys are CONSUMED here, so an
        // old file loads with no error and no crash, and neither is written
        // back: one generic path, no per-style migration, no diagnostic noise.
        continue;
      } else if (key == "palette") {
        config.visualizer_palette =
            std::string(ui::normalizeVisualizerPaletteId(unquote(value)));
      } else if (key == "refresh_hz") {
        readNumber(value, 5, 60, 60, &config.visualizer_refresh_hz,
                   diagnostics, "visualizer", "refresh_hz");
      } else if (key == "sensitivity") {
        readNumber(value, 0.1F, 5.0F, 1.0F, &config.visualizer_sensitivity,
                   diagnostics, "visualizer", "sensitivity");
      } else if (key == "bar_density") {
        readNumber(value, 32, 96, 64, &config.visualizer_bar_density,
                   diagnostics, "visualizer", "bar_density");
      } else if (key == "fifo_path") {
        readString(value, &config.visualizer_fifo);
      } else if (key == "sample_rate") {
        readNumber(value, 8000, 192000, 44100, &config.visualizer_sample_rate,
                   diagnostics, "visualizer", "sample_rate");
      } else if (key == "channels") {
        readNumber(value, 1, 8, 2, &config.visualizer_channels, diagnostics,
                   "visualizer", "channels");
      }
    }
  }
  if (!schema_seen)
    config.schema_version = kConfigSchemaVersion;
}

/// Groups the preserved entries by section, in first-seen order, so a rewritten
/// file keeps a recognizable shape instead of dumping every unknown key at the
/// end in a different grouping.
std::vector<std::string> preservedSections(const Config &config) {
  std::vector<std::string> order;
  for (const auto &entry : config.preserved) {
    if (std::find(order.begin(), order.end(), entry.section) == order.end())
      order.push_back(entry.section);
  }
  return order;
}

std::string serialize(const Config &config) {
  std::ostringstream output;
  output << "schema_version = " << config.schema_version << "\n\n"
         << "[general]\n"
         << "start_page = " << quote(pageId(config.start_page)) << '\n'
         << "stop_on_exit = " << (config.stop_on_exit ? "true" : "false")
         << '\n'
         << "seek_step = " << config.seek_step << '\n'
         << "volume_step = " << config.volume_step << "\n\n"
         << "[library]\n"
         << "path = " << quote(config.library_path) << "\n\n"
         << "[mpd]\n"
         << "host = " << quote(config.mpd_host) << '\n'
         << "port = " << config.mpd_port << '\n'
         << "password = " << quote(config.mpd_password) << '\n'
         << "timeout_ms = " << config.mpd_timeout_ms << '\n'
         << "auto_reconnect = " << (config.auto_reconnect ? "true" : "false")
         << "\n\n"
         << "[appearance]\n"
         << "theme = " << quote(config.theme_name) << '\n'
         << "theme_directory = " << quote(config.theme_directory) << '\n'
         << "icons = " << quote(config.icon_set) << "\n\n"
         << "[visualizer]\n"
         << "palette = " << quote(config.visualizer_palette) << '\n'
         << "refresh_hz = " << config.visualizer_refresh_hz << '\n'
         << "sensitivity = " << config.visualizer_sensitivity << '\n'
         << "bar_density = " << config.visualizer_bar_density << '\n'
         << "fifo_path = " << quote(config.visualizer_fifo) << '\n'
         << "sample_rate = " << config.visualizer_sample_rate << '\n'
         << "channels = " << config.visualizer_channels << "\n\n"
         << "[history]\n"
         << "enabled = " << (config.history_enabled ? "true" : "false") << '\n'
         << "max_entries = " << config.history_max_entries << "\n\n";
  // Only emit a table that actually has overrides: defaults live in the
  // application, so an empty table in the user file is noise.
  for (const auto &[context, actions] : config.keybindings) {
    if (actions.empty())
      continue;
    output << "[keybindings." << context << "]\n";
    for (const auto &[action_id, sequences] : actions) {
      if (sequences.empty())
        continue;
      output << action_id << " = ";
      if (sequences.size() == 1U) {
        output << quote(sequences.front());
      } else {
        output << '[';
        for (std::size_t index = 0; index < sequences.size(); ++index) {
          if (index > 0)
            output << ", ";
          output << quote(sequences[index]);
        }
        output << ']';
      }
      output << '\n';
    }
    output << '\n';
  }
  // Settings termusic does not model, put back exactly as they were written.
  for (const auto &section : preservedSections(config)) {
    output << "\n";
    if (!section.empty())
      output << '[' << section << "]\n";
    for (const auto &entry : config.preserved) {
      if (entry.section == section)
        output << entry.key << " = " << entry.value << '\n';
    }
  }
  return output.str();
}

} // namespace

std::vector<std::string> parseBindingList(const std::string &value) {
  std::vector<std::string> result;
  std::string body = trim(value);
  if (body.empty())
    return result;
  const bool list = body.front() == '[' && body.back() == ']';
  if (list)
    body = body.substr(1, body.size() - 2);
  else {
    const std::string sequence = unquote(body);
    if (!sequence.empty())
      result.push_back(sequence);
    return result;
  }
  std::string current;
  bool in_quotes = false;
  bool escaped = false;
  for (const char c : body) {
    if (escaped) {
      current += '\\';
      current += c;
      escaped = false;
      continue;
    }
    if (in_quotes && c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      in_quotes = !in_quotes;
      current += c;
      continue;
    }
    if (c == ',' && !in_quotes) {
      const std::string token = unquote(current);
      if (!token.empty())
        result.push_back(token);
      current.clear();
      continue;
    }
    current += c;
  }
  if (escaped || in_quotes)
    return {};
  const std::string token = unquote(current);
  if (!token.empty())
    result.push_back(token);
  return result;
}

std::string_view pageId(Page page) { return pageToken(page); }

std::optional<Page> parsePage(std::string_view value) {
  return pageFromToken(value);
}

ConfigStore::ConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

std::filesystem::path ConfigStore::defaultPath() {
  return resolveAppPaths().configFile();
}

bool ConfigStore::exists() const {
  if (path_.empty())
    return false;
  std::error_code error;
  return std::filesystem::is_regular_file(path_, error);
}

ConfigLoad ConfigStore::loadDetailed() const {
  ConfigLoad load;
  if (path_.empty()) {
    load.warnings.push_back(
        "no configuration directory (set HOME or XDG_CONFIG_HOME); "
        "using built-in defaults");
    return load;
  }
  std::ifstream input(path_);
  if (!input) {
    if (exists())
      load.warnings.push_back("cannot read " + path_.string() +
                              "; using built-in defaults");
    // A missing file is the NORMAL first run: no warning, no file created.
    return load;
  }
  parseDocument(input, &load);
  return load;
}

Config ConfigStore::load(std::string *warning) const {
  ConfigLoad load = loadDetailed();
  if (warning != nullptr) {
    std::vector<std::string> messages = load.warnings;
    messages.insert(messages.end(), load.errors.begin(), load.errors.end());
    std::string joined;
    for (const auto &message : messages) {
      if (!joined.empty())
        joined += "; ";
      joined += message;
    }
    *warning = joined;
  }
  return load.config;
}

bool ConfigStore::save(const Config &config, std::string *error) const {
  if (path_.empty()) {
    if (error != nullptr)
      *error =
          "cannot determine a configuration path (set HOME or XDG_CONFIG_HOME)";
    return false;
  }
  std::error_code filesystem_error;
  if (!path_.parent_path().empty())
    std::filesystem::create_directories(path_.parent_path(), filesystem_error);
  if (filesystem_error) {
    if (error != nullptr)
      *error = filesystem_error.message();
    return false;
  }

  const std::string text = serialize(config);

  // ATOMIC: the new document goes to a temporary file in the SAME directory,
  // is flushed to the disk, and only then replaces config.toml in one rename.
  // A crash, a full disk or a killed process therefore leaves either the old
  // file or the new one -- never a half-written config.
  std::filesystem::path temporary = path_;
  temporary += ".tmp";
#if !defined(_WIN32)
  const int descriptor =
      ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (descriptor < 0) {
    if (error != nullptr)
      *error = "cannot write " + temporary.string();
    return false;
  }
  std::size_t written = 0;
  bool ok = true;
  while (written < text.size()) {
    const ssize_t count =
        ::write(descriptor, text.data() + written, text.size() - written);
    if (count <= 0) {
      ok = false;
      break;
    }
    written += static_cast<std::size_t>(count);
  }
  if (ok)
    ok = ::fsync(descriptor) == 0;
  ::close(descriptor);
  if (!ok) {
    ::unlink(temporary.c_str());
    if (error != nullptr)
      *error = "failed while writing " + temporary.string();
    return false;
  }
  // The password is a secret, so a file that carries one is readable by its
  // owner only. A file without one keeps the usual permissions.
  if (!config.mpd_password.empty())
    (void)::chmod(temporary.c_str(), 0600);
#else
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      if (error != nullptr)
        *error = "cannot write " + temporary.string();
      return false;
    }
    output << text;
    output.flush();
    if (!output) {
      if (error != nullptr)
        *error = "failed while writing " + temporary.string();
      return false;
    }
  }
#endif
  std::filesystem::rename(temporary, path_, filesystem_error);
  if (filesystem_error) {
    std::filesystem::remove(temporary, filesystem_error);
    if (error != nullptr)
      *error = "cannot replace " + path_.string();
    return false;
  }
#if !defined(_WIN32)
  // Durability of the RENAME itself: without this the new name can be lost by a
  // power failure even though the file contents were flushed.
  const int directory = ::open(path_.parent_path().c_str(), O_RDONLY);
  if (directory >= 0) {
    (void)::fsync(directory);
    ::close(directory);
  }
#endif
  return true;
}

std::string defaultConfigText() {
  // A hand-written reference document. It is not generated from the writer on
  // purpose: the comments are the documentation. A unit test parses this text
  // and asserts it equals a default-constructed Config, which is what keeps the
  // two from drifting apart.
  return R"TOML(# termusic configuration -- schema version 1
#
# Every key is optional. Omitted keys keep the value shown here, which is also
# what termusic uses when this file does not exist at all.
#
# Precedence for the MPD endpoint: command line > environment > this file >
# built-in defaults. MPD_HOST and MPD_PORT are the conventional MPD variables
# and are honored; --config PATH and TERMUSIC_CONFIG choose this file.
#
# termusic is an MPD client. It never starts, stops or configures the MPD
# daemon; it only connects to it.

schema_version = 1

[general]
# Which page opens at startup: "library" (the Vault) or "core".
start_page = "library"
# true  = quitting stops MPD playback (the historical behaviour)
# false = quitting only disconnects; MPD keeps playing
stop_on_exit = true
seek_step = 5
volume_step = 5

[library]
# The directory MPD serves. Informational: MPD owns its own music_directory.
path = ""

[mpd]
# Empty host and port 0 mean "not configured here", so MPD_HOST/MPD_PORT and
# then the built-in default (127.0.0.1:6600) apply. A remote server is set the
# same way: host = "musicbox.lan".
host = "127.0.0.1"
port = 6600
# Only when the server requires one. Prefer this file (0600) or MPD_HOST's
# password@host form over the command line, which is visible in `ps`.
password = ""
# Socket timeout in milliseconds. Never 0: libmpdclient reads 0 as "wait
# forever", which would make a hung server unquittable.
timeout_ms = 2000
# Reconnect on its own, with a bounded retry interval, when the server goes away.
auto_reconnect = true

[appearance]
# Theme id: catppuccin-mocha (default), kanagawa, material-palenight,
# monokai-pro, github-dark, oxocarbon, catppuccin-macchiato, or a theme file in
# `theme_directory` below.
theme = "default"
# Empty means the `themes` directory next to this file.
theme_directory = ""
# "nerd" needs a patched font; "unicode" is the safe fallback.
icons = "nerd"

[visualizer]
# The Spectrum is the only visualizer: there is no style to choose and no
# switch to turn it off. A legacy `enabled` or `style` key is still accepted
# and ignored, so an old configuration keeps loading.
# theme | ice | fire | rainbow
palette = "theme"
refresh_hz = 60
sensitivity = 1
bar_density = 64
# MPD writes its spectrum here (audio_output type "fifo"); termusic reads it.
fifo_path = "/tmp/mpd.fifo"
sample_rate = 44100
channels = 2

[history]
# Application-owned playback history, in termusic's XDG data directory. MPD is
# never modified by it.
enabled = true
max_entries = 100

# Key bindings: only what you write here overrides a built-in binding.
# [keybindings.global]
# toggle_immersive = "i"
# [keybindings.tree]
# next = ["j", "Down"]
)TOML";
}

Environment Environment::current() {
  const auto read = [](const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
  };
  Environment environment;
  environment.mpd_host = read("MPD_HOST");
  environment.mpd_port = read("MPD_PORT");
  environment.termusic_config = read("TERMUSIC_CONFIG");
  return environment;
}

ConnectionSettings resolveConnection(const Config &config,
                                     const CliOverrides &cli,
                                     const Environment &environment,
                                     bool config_file_present) {
  ConnectionSettings settings;
  const char *file_source =
      config_file_present ? "config.toml" : "default";

  // --- host -----------------------------------------------------------------
  // MPD_HOST may carry a password ("password@host") and may name a local
  // socket by absolute path, which is the libmpdclient convention every other
  // MPD client on the machine follows.
  std::string env_host = environment.mpd_host;
  std::string env_password;
  if (const auto at = env_host.find('@'); at != std::string::npos) {
    const std::string before = env_host.substr(0, at);
    const std::string after = env_host.substr(at + 1);
    if (!after.empty()) {
      env_password = before;
      env_host = after;
    }
  }
  if (cli.host_set && !cli.host.empty()) {
    settings.host = cli.host;
    settings.host_source = "command line";
  } else if (!env_host.empty()) {
    settings.host = env_host;
    settings.host_source = "environment (MPD_HOST)";
  } else if (!config.mpd_host.empty()) {
    settings.host = config.mpd_host;
    settings.host_source = file_source;
  } else {
    settings.host = std::string(kDefaultMpdHost);
    settings.host_source = "default";
  }

  // --- port -----------------------------------------------------------------
  if (cli.port_set) {
    settings.port = cli.port;
    settings.port_source = "command line";
  } else if (!environment.mpd_port.empty()) {
    int parsed = 0;
    if (parseNumber(environment.mpd_port, &parsed) && parsed > 0 &&
        parsed <= 65535) {
      settings.port = parsed;
      settings.port_source = "environment (MPD_PORT)";
    } else {
      settings.port = kDefaultMpdPort;
      settings.port_source = "default (MPD_PORT is not a port number)";
    }
  } else if (config.mpd_port != 0) {
    settings.port = config.mpd_port;
    settings.port_source = file_source;
  } else {
    settings.port = kDefaultMpdPort;
    settings.port_source = "default";
  }

  // --- password -------------------------------------------------------------
  if (cli.password_set) {
    settings.password = cli.password;
    settings.password_source = "command line";
  } else if (!env_password.empty()) {
    settings.password = env_password;
    settings.password_source = "environment (MPD_HOST)";
  } else if (!config.mpd_password.empty()) {
    settings.password = config.mpd_password;
    settings.password_source = file_source;
  } else {
    settings.password_source = "default (none)";
  }

  // --- timeout --------------------------------------------------------------
  if (cli.timeout_set) {
    settings.timeout_ms = cli.timeout_ms;
    settings.timeout_source = "command line";
  } else {
    settings.timeout_ms = config.mpd_timeout_ms;
    settings.timeout_source = file_source;
  }
  // The "never zero" contract survives every layer, including the command line.
  if (settings.timeout_ms < 100 || settings.timeout_ms > 60000) {
    settings.timeout_ms = kDefaultMpdTimeoutMs;
    settings.timeout_source += " (out of range; using the default)";
  }

  settings.auto_reconnect = config.auto_reconnect;
  settings.auto_reconnect_source = file_source;
  return settings;
}

std::vector<std::string>
keymapConflicts(const std::map<Action, std::string> &keymap) {
  std::vector<std::string> result;
  for (auto lhs = keymap.begin(); lhs != keymap.end(); ++lhs) {
    if (lhs->second.empty())
      continue;
    for (auto rhs = std::next(lhs); rhs != keymap.end(); ++rhs) {
      if (lhs->second == rhs->second &&
          !allowedContextDuplicate(lhs->first, rhs->first)) {
        result.push_back(lhs->second + ": " +
                         std::string(actionLabel(lhs->first)) + " / " +
                         std::string(actionLabel(rhs->first)));
      }
    }
  }
  return result;
}

} // namespace termusic
