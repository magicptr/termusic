#include "ui/theme.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace termusic::ui {
namespace {

std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::string unquote(std::string_view value) {
  std::string result = trim(value);
  if (result.size() >= 2 && result.front() == '"' && result.back() == '"')
    result = result.substr(1, result.size() - 2);
  return result;
}

bool validId(std::string_view id) {
  return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '_';
  });
}

std::optional<ftxui::Color> parseColor(std::string_view input) {
  std::string value = unquote(input);
  if (value.size() != 7 || value.front() != '#')
    return std::nullopt;
  unsigned rgb = 0;
  std::istringstream stream(value.substr(1));
  stream >> std::hex >> rgb;
  if (!stream || !stream.eof())
    return std::nullopt;
  return ftxui::Color::RGB(static_cast<uint8_t>((rgb >> 16U) & 0xffU),
                           static_cast<uint8_t>((rgb >> 8U) & 0xffU),
                           static_cast<uint8_t>(rgb & 0xffU));
}

using ColorMember = ftxui::Color Theme::*;
const std::unordered_map<std::string, ColorMember> &colorMembers() {
  static const std::unordered_map<std::string, ColorMember> members = {
      {"background", &Theme::background},
      {"background_deep", &Theme::background_deep},
      {"panel", &Theme::panel},
      {"text", &Theme::text},
      {"muted_text", &Theme::muted_text},
      {"weak_text", &Theme::weak_text},
      {"header_text", &Theme::header_text},
      {"border", &Theme::border},
      {"border_dim", &Theme::border_dim},
      {"separator", &Theme::separator},
      {"accent_primary", &Theme::accent_primary},
      {"accent_secondary", &Theme::accent_secondary},
      {"accent_purple", &Theme::accent_purple},
      {"brand", &Theme::brand},
      {"frame", &Theme::frame},
      {"selected_fg", &Theme::selected_fg},
      {"selected_bg", &Theme::selected_bg},
      {"tree_cursor_bg", &Theme::tree_cursor_bg},
      {"tree_cursor_fg", &Theme::tree_cursor_fg},
      {"track_cursor_bg", &Theme::track_cursor_bg},
      {"track_cursor_fg", &Theme::track_cursor_fg},
      {"active_collection", &Theme::active_collection},
      {"playing", &Theme::playing},
      {"tree_item", &Theme::tree_item},
      {"surface", &Theme::surface},
      {"divider", &Theme::divider},
      {"visual_cursor_bg", &Theme::visual_cursor_bg},
      {"volume_fill", &Theme::volume_fill},
      {"volume_empty", &Theme::volume_empty},
      {"success", &Theme::success},
      {"warning", &Theme::warning},
      {"info", &Theme::info},
      {"hover_bg", &Theme::hover_bg},
      {"hover_border", &Theme::hover_border},
      {"progress_filled", &Theme::progress_filled},
      {"progress_filled_end", &Theme::progress_filled_end},
      {"progress_empty", &Theme::progress_empty},
      {"icon", &Theme::icon},
      {"progress_knob", &Theme::progress_knob},
      {"spectrum_low", &Theme::spectrum_low},
      {"spectrum_high", &Theme::spectrum_high},
      {"spectrum_peak", &Theme::spectrum_peak},
      {"error", &Theme::error},
      {"ok", &Theme::ok},
  };
  return members;
}

Theme nordTheme() {
  Theme theme;
  theme.background = ftxui::Color::RGB(0x2E, 0x34, 0x40);
  theme.background_deep = ftxui::Color::RGB(0x24, 0x29, 0x33);
  theme.panel = ftxui::Color::RGB(0x3B, 0x42, 0x52);
  theme.text = ftxui::Color::RGB(0xEC, 0xEF, 0xF4);
  theme.muted_text = ftxui::Color::RGB(0xD8, 0xDE, 0xE9);
  theme.weak_text = ftxui::Color::RGB(0x81, 0xA1, 0xC1);
  theme.header_text = theme.text;
  theme.border = ftxui::Color::RGB(0x88, 0xC0, 0xD0);
  theme.border_dim = ftxui::Color::RGB(0x4C, 0x56, 0x6A);
  theme.separator = theme.border_dim;
  theme.accent_primary = ftxui::Color::RGB(0xB4, 0x8E, 0xAD);
  theme.accent_secondary = ftxui::Color::RGB(0x88, 0xC0, 0xD0);
  theme.accent_purple = theme.accent_primary;
  theme.brand = theme.accent_primary;
  theme.frame = theme.text;
  theme.selected_fg = theme.background_deep;
  theme.selected_bg = ftxui::Color::RGB(0x88, 0xC0, 0xD0);
  // Keep every focus/selection role derived from this preset's own palette
  // rather than inheriting the Catppuccin defaults.
  theme.tree_cursor_bg = theme.accent_primary;
  theme.tree_cursor_fg = theme.background_deep;
  theme.track_cursor_bg = theme.accent_purple;
  theme.track_cursor_fg = theme.background_deep;
  theme.visual_cursor_bg = theme.accent_purple;
  theme.active_collection = theme.accent_secondary;
  theme.playing = theme.accent_secondary;
  theme.tree_item = theme.accent_primary;
  theme.hover_bg = ftxui::Color::RGB(0x43, 0x4C, 0x5E);
  theme.visual_selection_bg = theme.hover_bg;
  theme.surface = theme.hover_bg;
  theme.hover_border = theme.accent_primary;
  theme.progress_filled = theme.accent_secondary;
  theme.progress_filled_end = theme.accent_primary;
  theme.progress_empty = theme.border_dim;
  theme.volume_fill = theme.spectrum_low;
  theme.volume_empty = theme.border_dim;
  theme.divider = theme.border_dim;
  theme.icon = theme.text;
  theme.progress_knob = theme.accent_primary;
  theme.spectrum_low = ftxui::Color::RGB(0x8F, 0xBC, 0xBB);
  theme.spectrum_high = theme.accent_secondary;
  theme.spectrum_peak = ftxui::Color::RGB(0xEB, 0xCB, 0x8B);
  theme.error = ftxui::Color::RGB(0xBF, 0x61, 0x6A);
  theme.warning = theme.spectrum_peak;
  theme.success = ftxui::Color::RGB(0xA3, 0xBE, 0x8C);
  theme.info = theme.accent_secondary;
  theme.ok = theme.success;
  return theme;
}

} // namespace

ThemeRegistry::ThemeRegistry() {
  // Catppuccin Mocha is the application default, and it is registered FIRST:
  // an unknown or legacy theme id (including the historical "default")
  // resolves to entries_.front(), so existing configuration keeps working and
  // picks up the new palette.
  registerTheme("catppuccin-mocha", "Catppuccin Mocha", Theme{});
  // Alternate presets are kept exactly as they were; the Mocha values must
  // never overwrite them.
  registerTheme("nord", "Nord", nordTheme());
}

bool ThemeRegistry::registerTheme(std::string id, std::string name, Theme theme,
                                  std::filesystem::path source,
                                  std::string *error) {
  if (!validId(id)) {
    if (error)
      *error = "theme id must contain only letters, digits, '-' or '_'";
    return false;
  }
  if (name.empty())
    name = id;
  if (std::any_of(entries_.begin(), entries_.end(),
                  [&](const Entry &entry) { return entry.info.id == id; })) {
    if (error)
      *error = "duplicate theme id: " + id;
    return false;
  }
  entries_.push_back(
      {ThemeInfo{std::move(id), std::move(name), std::move(source)},
       std::move(theme)});
  return true;
}

bool ThemeRegistry::loadFile(const std::filesystem::path &path,
                             std::string *error) {
  std::ifstream input(path);
  if (!input) {
    if (error)
      *error = "cannot read theme file " + path.string();
    return false;
  }
  Theme theme = resolve("default");
  std::string id = path.stem().string();
  std::string name = id;
  std::string section;
  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);
    if (line.empty() || line.front() == '#')
      continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(std::string_view(line).substr(1, line.size() - 2));
      continue;
    }
    const auto equal = line.find('=');
    if (equal == std::string::npos)
      continue;
    const std::string key = trim(std::string_view(line).substr(0, equal));
    const std::string value = trim(std::string_view(line).substr(equal + 1));
    if (section == "theme") {
      if (key == "id")
        id = unquote(value);
      else if (key == "name")
        name = unquote(value);
      continue;
    }
    if (section != "colors")
      continue;
    const auto member = colorMembers().find(key);
    const auto color = parseColor(value);
    if (member == colorMembers().end() || !color) {
      if (error)
        *error = path.string() + ":" + std::to_string(line_number) +
                 ": invalid color field '" + key + "'";
      return false;
    }
    theme.*(member->second) = *color;
  }
  return registerTheme(std::move(id), std::move(name), std::move(theme), path,
                       error);
}

std::size_t ThemeRegistry::loadDirectory(const std::filesystem::path &directory,
                                         std::vector<std::string> *warnings) {
  std::error_code error;
  if (!std::filesystem::exists(directory, error))
    return 0;
  std::vector<std::filesystem::path> files;
  for (std::filesystem::directory_iterator it(directory, error), end;
       !error && it != end; it.increment(error)) {
    if (it->is_regular_file() && it->path().extension() == ".toml")
      files.push_back(it->path());
  }
  if (error && warnings)
    warnings->push_back("cannot scan theme directory: " + error.message());
  std::sort(files.begin(), files.end());
  std::size_t loaded = 0;
  for (const auto &file : files) {
    std::string message;
    if (loadFile(file, &message))
      ++loaded;
    else if (warnings)
      warnings->push_back(std::move(message));
  }
  return loaded;
}

const Theme &ThemeRegistry::resolve(std::string_view id) const {
  const auto found =
      std::find_if(entries_.begin(), entries_.end(),
                   [&](const Entry &entry) { return entry.info.id == id; });
  return found == entries_.end() ? entries_.front().theme : found->theme;
}

std::string ThemeRegistry::resolveId(std::string_view id) const {
  const auto found =
      std::find_if(entries_.begin(), entries_.end(),
                   [&](const Entry &entry) { return entry.info.id == id; });
  return found == entries_.end() ? entries_.front().info.id : found->info.id;
}

std::string ThemeRegistry::nextId(std::string_view current) const {
  const auto found =
      std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
        return entry.info.id == current;
      });
  if (found == entries_.end() || std::next(found) == entries_.end())
    return entries_.front().info.id;
  return std::next(found)->info.id;
}

std::vector<ThemeInfo> ThemeRegistry::list() const {
  std::vector<ThemeInfo> result;
  result.reserve(entries_.size());
  for (const auto &entry : entries_)
    result.push_back(entry.info);
  return result;
}

const Theme &defaultTheme() {
  static const Theme theme;
  return theme;
}

} // namespace termusic::ui
