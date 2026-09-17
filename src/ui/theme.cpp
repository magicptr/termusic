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

/// Kanagawa (rebelot/kanagawa.nvim, "wave"): sumi ink surfaces, old-white
/// text, oni violet and crystal blue as the two accents, sakura pink for the
/// brand. Values are the upstream named colours.
Theme kanagawaTheme() {
  Theme theme;
  theme.background = ftxui::Color::RGB(0x1F, 0x1F, 0x28);      // sumiInk3
  theme.background_deep = ftxui::Color::RGB(0x16, 0x16, 0x1D); // sumiInk0
  theme.panel = ftxui::Color::RGB(0x2A, 0x2A, 0x37);           // sumiInk4
  theme.surface = ftxui::Color::RGB(0x36, 0x36, 0x46);         // sumiInk5
  theme.text = ftxui::Color::RGB(0xDC, 0xD7, 0xBA);        // fujiWhite
  theme.header_text = ftxui::Color::RGB(0xC8, 0xC0, 0x93); // oldWhite
  theme.muted_text = ftxui::Color::RGB(0xC8, 0xC0, 0x93);  // oldWhite
  theme.weak_text = ftxui::Color::RGB(0x72, 0x71, 0x69);   // fujiGray
  theme.border = ftxui::Color::RGB(0x54, 0x54, 0x6D);      // sumiInk6
  theme.border_dim = ftxui::Color::RGB(0x2A, 0x2A, 0x37);  // sumiInk4
  theme.divider = theme.border_dim;
  theme.separator = theme.border_dim;
  theme.accent_primary = ftxui::Color::RGB(0x95, 0x7F, 0xB8);   // oniViolet
  theme.accent_secondary = ftxui::Color::RGB(0x7E, 0x9C, 0xD8); // crystalBlue
  theme.accent_purple = ftxui::Color::RGB(0x93, 0x8A, 0xA9);    // springViolet1
  theme.brand = ftxui::Color::RGB(0xD2, 0x7E, 0x99);            // sakuraPink
  theme.frame = theme.text;
  theme.selected_fg = theme.background_deep;
  theme.selected_bg = theme.accent_primary;
  // Every focus/selection role comes from THIS palette: the tree cursor is the
  // strongest accent band, the track cursor the second accent, and the active
  // collection only a text tint, so Focus always outweighs Active.
  theme.tree_cursor_bg = theme.accent_primary;
  theme.tree_cursor_fg = theme.background_deep;
  theme.track_cursor_bg = theme.accent_secondary;
  theme.track_cursor_fg = theme.background_deep;
  theme.visual_cursor_bg = theme.accent_secondary;
  theme.visual_selection_bg = theme.surface;
  theme.active_collection = ftxui::Color::RGB(0x7A, 0xA8, 0x9F); // waveAqua2
  theme.playing = ftxui::Color::RGB(0x9C, 0xAB, 0xCA);           // springViolet2
  theme.tree_item = theme.accent_secondary;
  theme.hover_bg = theme.surface;
  theme.hover_border = theme.accent_primary;
  theme.progress_filled = theme.accent_primary;
  theme.progress_filled_end = theme.brand;
  theme.progress_empty = theme.border_dim;
  theme.progress_knob = theme.accent_purple;
  theme.volume_fill = theme.active_collection;
  theme.volume_empty = theme.border_dim;
  theme.icon = theme.header_text;
  theme.spectrum_low = theme.accent_primary;
  theme.spectrum_high = theme.accent_secondary;
  theme.spectrum_peak = ftxui::Color::RGB(0xE6, 0xC3, 0x84); // carpYellow
  theme.success = ftxui::Color::RGB(0x98, 0xBB, 0x6C);       // springGreen
  theme.warning = ftxui::Color::RGB(0xDC, 0xA5, 0x61);       // autumnYellow
  theme.error = ftxui::Color::RGB(0xE4, 0x68, 0x76);         // waveRed
  theme.info = theme.accent_secondary;
  theme.ok = theme.success;
  return theme;
}

/// Material Palenight (the official Material Theme "Palenight" palette): deep
/// indigo surfaces, lavender text, purple and blue accents.
Theme materialPalenightTheme() {
  Theme theme;
  theme.background = ftxui::Color::RGB(0x29, 0x2D, 0x3E);
  theme.background_deep = ftxui::Color::RGB(0x1B, 0x1E, 0x2B);
  theme.panel = ftxui::Color::RGB(0x33, 0x37, 0x47);
  theme.surface = ftxui::Color::RGB(0x3A, 0x3F, 0x58);
  theme.text = ftxui::Color::RGB(0xEE, 0xFF, 0xFF);
  theme.header_text = ftxui::Color::RGB(0xEE, 0xFF, 0xFF);
  theme.muted_text = ftxui::Color::RGB(0xA6, 0xAC, 0xCD);
  theme.weak_text = ftxui::Color::RGB(0x67, 0x6E, 0x95); // comments
  theme.border = ftxui::Color::RGB(0x44, 0x42, 0x67);
  theme.border_dim = ftxui::Color::RGB(0x33, 0x37, 0x47);
  theme.divider = theme.border_dim;
  theme.separator = theme.border_dim;
  theme.accent_primary = ftxui::Color::RGB(0xC7, 0x92, 0xEA);   // purple
  theme.accent_secondary = ftxui::Color::RGB(0x82, 0xAA, 0xFF); // blue
  theme.accent_purple = ftxui::Color::RGB(0xC7, 0x92, 0xEA);
  theme.brand = ftxui::Color::RGB(0xC7, 0x92, 0xEA);
  theme.frame = theme.text;
  theme.selected_fg = theme.background_deep;
  theme.selected_bg = theme.accent_primary;
  theme.tree_cursor_bg = ftxui::Color::RGB(0xF0, 0x71, 0x78); // pink
  theme.tree_cursor_fg = theme.background_deep;
  theme.track_cursor_bg = theme.accent_primary;
  theme.track_cursor_fg = theme.background_deep;
  theme.visual_cursor_bg = theme.accent_primary;
  theme.visual_selection_bg = theme.surface;
  theme.active_collection = theme.accent_secondary;
  theme.playing = ftxui::Color::RGB(0x89, 0xDD, 0xFF); // cyan
  theme.tree_item = theme.tree_cursor_bg;
  theme.hover_bg = theme.surface;
  theme.hover_border = theme.accent_primary;
  theme.progress_filled = theme.accent_secondary;
  theme.progress_filled_end = theme.accent_primary;
  theme.progress_empty = theme.border_dim;
  theme.progress_knob = theme.accent_primary;
  theme.volume_fill = theme.playing;
  theme.volume_empty = theme.border_dim;
  theme.icon = theme.header_text;
  theme.spectrum_low = theme.accent_primary;
  theme.spectrum_high = theme.accent_secondary;
  theme.spectrum_peak = ftxui::Color::RGB(0xFF, 0xCB, 0x6B); // yellow
  theme.success = ftxui::Color::RGB(0xC3, 0xE8, 0x8D);       // green
  theme.warning = ftxui::Color::RGB(0xFF, 0xCB, 0x6B);       // yellow
  theme.error = ftxui::Color::RGB(0xFF, 0x53, 0x70);         // red
  theme.info = theme.accent_secondary;
  theme.ok = theme.success;
  return theme;
}

/// Monokai Pro ("classic"): warm charcoal surfaces and the six signature
/// accents, with the yellow/orange pair reserved for warnings and peaks.
Theme monokaiProTheme() {
  Theme theme;
  theme.background = ftxui::Color::RGB(0x2D, 0x2A, 0x2E);
  theme.background_deep = ftxui::Color::RGB(0x22, 0x1F, 0x22);
  theme.panel = ftxui::Color::RGB(0x40, 0x3E, 0x41);
  theme.surface = ftxui::Color::RGB(0x5B, 0x59, 0x5C);
  theme.text = ftxui::Color::RGB(0xFC, 0xFC, 0xFA);
  theme.header_text = ftxui::Color::RGB(0xFC, 0xFC, 0xFA);
  theme.muted_text = ftxui::Color::RGB(0xC1, 0xC0, 0xC0);
  theme.weak_text = ftxui::Color::RGB(0x72, 0x70, 0x72); // comments
  theme.border = ftxui::Color::RGB(0x5B, 0x59, 0x5C);
  theme.border_dim = ftxui::Color::RGB(0x40, 0x3E, 0x41);
  theme.divider = theme.border_dim;
  theme.separator = theme.border_dim;
  theme.accent_primary = ftxui::Color::RGB(0xAB, 0x9D, 0xF2);   // purple
  theme.accent_secondary = ftxui::Color::RGB(0x78, 0xDC, 0xE8); // cyan
  theme.accent_purple = ftxui::Color::RGB(0xAB, 0x9D, 0xF2);
  theme.brand = ftxui::Color::RGB(0xFF, 0x61, 0x88); // pink
  theme.frame = theme.text;
  theme.selected_fg = theme.background_deep;
  theme.selected_bg = theme.accent_primary;
  theme.tree_cursor_bg = theme.accent_primary;
  theme.tree_cursor_fg = theme.background_deep;
  theme.track_cursor_bg = theme.accent_secondary;
  theme.track_cursor_fg = theme.background_deep;
  theme.visual_cursor_bg = theme.accent_secondary;
  theme.visual_selection_bg = theme.surface;
  theme.active_collection = ftxui::Color::RGB(0xFC, 0x98, 0x67); // orange
  theme.playing = theme.accent_secondary;
  theme.tree_item = theme.accent_primary;
  theme.hover_bg = theme.surface;
  theme.hover_border = theme.accent_primary;
  theme.progress_filled = theme.accent_secondary;
  theme.progress_filled_end = theme.accent_primary;
  theme.progress_empty = theme.border_dim;
  theme.progress_knob = theme.accent_primary;
  theme.volume_fill = ftxui::Color::RGB(0xA9, 0xDC, 0x76); // green
  theme.volume_empty = theme.border_dim;
  theme.icon = theme.header_text;
  theme.spectrum_low = theme.accent_primary;
  theme.spectrum_high = theme.accent_secondary;
  theme.spectrum_peak = ftxui::Color::RGB(0xFF, 0xD8, 0x66); // yellow
  theme.success = ftxui::Color::RGB(0xA9, 0xDC, 0x76);       // green
  theme.warning = ftxui::Color::RGB(0xFF, 0xD8, 0x66);       // yellow
  theme.error = ftxui::Color::RGB(0xFF, 0x61, 0x88);         // pink/red
  theme.info = theme.accent_secondary;
  theme.ok = theme.success;
  return theme;
}

/// GitHub Dark (Primer's dark primitives): near-black canvas, restrained
/// greys, blue as the single strong accent.
Theme githubDarkTheme() {
  Theme theme;
  theme.background = ftxui::Color::RGB(0x0D, 0x11, 0x17);      // canvas.default
  theme.background_deep = ftxui::Color::RGB(0x01, 0x04, 0x09); // canvas.inset
  theme.panel = ftxui::Color::RGB(0x16, 0x1B, 0x22);           // canvas.subtle
  theme.surface = ftxui::Color::RGB(0x21, 0x26, 0x2D);         // border.muted
  theme.text = ftxui::Color::RGB(0xE6, 0xED, 0xF3);        // fg.default
  theme.header_text = ftxui::Color::RGB(0xC9, 0xD1, 0xD9); // fg.default (dim)
  theme.muted_text = ftxui::Color::RGB(0x8B, 0x94, 0x9E);  // fg.muted
  theme.weak_text = ftxui::Color::RGB(0x6E, 0x76, 0x81);   // fg.subtle
  theme.border = ftxui::Color::RGB(0x30, 0x36, 0x3D);      // border.default
  theme.border_dim = ftxui::Color::RGB(0x21, 0x26, 0x2D);  // border.muted
  theme.divider = theme.border_dim;
  theme.separator = theme.border_dim;
  theme.accent_primary = ftxui::Color::RGB(0x58, 0xA6, 0xFF);   // accent.fg
  theme.accent_secondary = ftxui::Color::RGB(0x79, 0xC0, 0xFF); // blue.3
  theme.accent_purple = ftxui::Color::RGB(0xA3, 0x71, 0xF7);    // done.fg
  theme.brand = ftxui::Color::RGB(0xDB, 0x61, 0xA2);            // sponsors
  theme.frame = theme.text;
  theme.selected_fg = theme.background_deep;
  theme.selected_bg = theme.accent_primary;
  theme.tree_cursor_bg = theme.accent_primary;
  theme.tree_cursor_fg = theme.background_deep;
  theme.track_cursor_bg = theme.accent_purple;
  theme.track_cursor_fg = theme.background_deep;
  theme.visual_cursor_bg = theme.accent_purple;
  theme.visual_selection_bg = theme.surface;
  theme.active_collection = theme.accent_purple;
  theme.playing = ftxui::Color::RGB(0x3F, 0xB9, 0x50); // success.fg
  theme.tree_item = theme.brand;
  theme.hover_bg = theme.surface;
  theme.hover_border = theme.accent_primary;
  theme.progress_filled = theme.accent_primary;
  theme.progress_filled_end = theme.accent_purple;
  theme.progress_empty = theme.border_dim;
  theme.progress_knob = theme.accent_purple;
  theme.volume_fill = theme.accent_secondary;
  theme.volume_empty = theme.border_dim;
  theme.icon = theme.header_text;
  theme.spectrum_low = theme.accent_primary;
  theme.spectrum_high = theme.accent_purple;
  theme.spectrum_peak = ftxui::Color::RGB(0xD2, 0x99, 0x22); // attention.fg
  theme.success = ftxui::Color::RGB(0x3F, 0xB9, 0x50);       // success.fg
  theme.warning = ftxui::Color::RGB(0xD2, 0x99, 0x22);       // attention.fg
  theme.error = ftxui::Color::RGB(0xF8, 0x51, 0x49);         // danger.fg
  theme.info = theme.accent_primary;
  theme.ok = theme.success;
  return theme;
}

/// Oxocarbon (nyoom-engineering/oxocarbon.nvim): near-black surfaces with a
/// restrained grey ramp and the palette's teal/purple/blue/pink accents.
Theme oxocarbonTheme() {
  Theme theme;
  theme.background = ftxui::Color::RGB(0x16, 0x16, 0x16);      // base00
  theme.background_deep = ftxui::Color::RGB(0x13, 0x13, 0x13); // blend
  theme.panel = ftxui::Color::RGB(0x29, 0x29, 0x29);           // base01
  theme.surface = ftxui::Color::RGB(0x3F, 0x3F, 0x3F);         // base02
  theme.text = ftxui::Color::RGB(0xF2, 0xF4, 0xF8);       // base05
  theme.header_text = ftxui::Color::RGB(0xF2, 0xF4, 0xF8); // base05
  theme.muted_text = ftxui::Color::RGB(0xD2, 0xD2, 0xD2);  // base04
  theme.weak_text = ftxui::Color::RGB(0x52, 0x52, 0x52);   // base03
  theme.border = ftxui::Color::RGB(0x5A, 0x5A, 0x5A);      // base03 light
  theme.border_dim = ftxui::Color::RGB(0x29, 0x29, 0x29);  // base01
  theme.divider = theme.border_dim;
  theme.separator = theme.border_dim;
  theme.accent_primary = ftxui::Color::RGB(0xBE, 0x95, 0xFF);   // purple
  theme.accent_secondary = ftxui::Color::RGB(0x33, 0xB1, 0xFF); // blue
  theme.accent_purple = ftxui::Color::RGB(0xBE, 0x95, 0xFF);    // purple
  theme.brand = ftxui::Color::RGB(0xFF, 0x7E, 0xB6);            // pink
  theme.frame = theme.text;
  theme.selected_fg = theme.background_deep;
  theme.selected_bg = theme.accent_primary;
  theme.tree_cursor_bg = theme.accent_primary;
  theme.tree_cursor_fg = theme.background_deep;
  theme.track_cursor_bg = theme.accent_secondary;
  theme.track_cursor_fg = theme.background_deep;
  theme.visual_cursor_bg = theme.accent_secondary;
  theme.visual_selection_bg = theme.surface;
  theme.active_collection = ftxui::Color::RGB(0x82, 0xCF, 0xFF); // pale cyan
  theme.playing = ftxui::Color::RGB(0x08, 0xBD, 0xBA);           // teal
  theme.tree_item = theme.active_collection;
  theme.hover_bg = theme.surface;
  theme.hover_border = theme.accent_primary;
  theme.progress_filled = theme.accent_primary;
  theme.progress_filled_end = theme.accent_secondary;
  theme.progress_empty = theme.border_dim;
  theme.progress_knob = theme.accent_primary;
  theme.volume_fill = theme.playing;
  theme.volume_empty = theme.border_dim;
  theme.icon = theme.header_text;
  theme.spectrum_low = theme.accent_primary;
  theme.spectrum_high = theme.accent_secondary;
  theme.spectrum_peak = theme.brand;
  theme.success = ftxui::Color::RGB(0x42, 0xBE, 0x65); // green
  theme.warning = ftxui::Color::RGB(0xFF, 0x6F, 0x00); // orange
  theme.error = ftxui::Color::RGB(0xEE, 0x53, 0x96);   // magenta
  theme.info = theme.accent_secondary;
  theme.ok = theme.success;
  return theme;
}

/// Catppuccin Macchiato: the official Macchiato palette, one step lighter than
/// Mocha, with the same role mapping the default theme uses.
Theme catppuccinMacchiatoTheme() {
  Theme theme;
  theme.background = ftxui::Color::RGB(0x24, 0x27, 0x3A);      // Base
  theme.background_deep = ftxui::Color::RGB(0x18, 0x19, 0x26); // Crust
  theme.panel = ftxui::Color::RGB(0x1E, 0x20, 0x30);           // Mantle
  theme.surface = ftxui::Color::RGB(0x36, 0x3A, 0x4F);         // Surface0
  theme.text = ftxui::Color::RGB(0xCA, 0xD3, 0xF5);        // Text
  theme.header_text = ftxui::Color::RGB(0xB8, 0xC0, 0xE0); // Subtext1
  theme.muted_text = ftxui::Color::RGB(0xA5, 0xAD, 0xCB);  // Subtext0
  theme.weak_text = ftxui::Color::RGB(0x80, 0x87, 0xA2);   // Overlay1
  theme.border = ftxui::Color::RGB(0x49, 0x4D, 0x64);      // Surface1
  theme.border_dim = ftxui::Color::RGB(0x36, 0x3A, 0x4F);  // Surface0
  theme.divider = theme.border;
  theme.separator = theme.border;
  theme.accent_primary = ftxui::Color::RGB(0xC6, 0xA0, 0xF6);   // Mauve
  theme.accent_secondary = ftxui::Color::RGB(0x7D, 0xC4, 0xE4); // Sapphire
  theme.accent_purple = ftxui::Color::RGB(0xB7, 0xBD, 0xF8);    // Lavender
  theme.brand = ftxui::Color::RGB(0xF5, 0xBD, 0xE6);            // Pink
  theme.frame = theme.text;
  theme.selected_fg = theme.background_deep;
  theme.selected_bg = theme.accent_primary;
  theme.tree_cursor_bg = theme.brand;
  theme.tree_cursor_fg = theme.background_deep;
  theme.track_cursor_bg = theme.accent_primary;
  theme.track_cursor_fg = theme.background_deep;
  theme.visual_cursor_bg = theme.accent_primary;
  theme.visual_selection_bg = theme.border;
  theme.active_collection = theme.accent_secondary;
  theme.playing = ftxui::Color::RGB(0x91, 0xD7, 0xE3); // Sky
  theme.tree_item = theme.brand;
  theme.hover_bg = theme.surface;
  theme.hover_border = ftxui::Color::RGB(0x6E, 0x73, 0x8D); // Overlay0
  theme.progress_filled = theme.accent_primary;
  theme.progress_filled_end = theme.accent_primary;
  theme.progress_empty = theme.surface;
  theme.progress_knob = theme.accent_purple;
  theme.volume_fill = ftxui::Color::RGB(0x8B, 0xD5, 0xCA); // Teal
  theme.volume_empty = theme.surface;
  theme.icon = theme.header_text;
  theme.spectrum_low = theme.accent_primary;
  theme.spectrum_high = theme.brand;
  theme.spectrum_peak = theme.brand;
  theme.success = ftxui::Color::RGB(0xA6, 0xDA, 0x95); // Green
  theme.warning = ftxui::Color::RGB(0xEE, 0xD4, 0x9F); // Yellow
  theme.error = ftxui::Color::RGB(0xED, 0x87, 0x96);   // Red
  theme.info = theme.accent_secondary;
  theme.ok = theme.success;
  return theme;
}

/// Crimson: a deliberately compact red ramp over near-black warm surfaces.
/// Every semantic role is mapped from the nine supplied colours, so no blue,
/// purple or cool-grey role leaks in from the default theme.
Theme crimsonTheme() {
  Theme theme;
  const auto near_black = ftxui::Color::RGB(0x10, 0x10, 0x10);
  const auto deep_black = ftxui::Color::RGB(0x18, 0x14, 0x14);
  const auto crimson = ftxui::Color::RGB(0xE5, 0x48, 0x4D);
  const auto coral = ftxui::Color::RGB(0xFF, 0x5A, 0x5F);
  const auto wine = ftxui::Color::RGB(0x8F, 0x28, 0x31);
  const auto warm_white = ftxui::Color::RGB(0xE8, 0xE3, 0xE3);
  const auto muted_red = ftxui::Color::RGB(0x8F, 0x85, 0x85);
  const auto dark_text = ftxui::Color::RGB(0x51, 0x4A, 0x4A);
  const auto pale_border = ftxui::Color::RGB(0xC9, 0xC3, 0xC3);

  theme.background = near_black;
  theme.background_deep = near_black;
  theme.panel = deep_black;
  theme.surface = deep_black;
  theme.text = warm_white;
  theme.header_text = warm_white;
  theme.muted_text = muted_red;
  theme.weak_text = dark_text;
  theme.border = pale_border;
  theme.border_dim = dark_text;
  theme.divider = dark_text;
  theme.separator = dark_text;
  theme.accent_primary = crimson;
  theme.accent_secondary = coral;
  theme.accent_purple = wine;
  theme.brand = coral;
  theme.frame = pale_border;
  theme.selected_fg = near_black;
  theme.selected_bg = crimson;
  theme.tree_cursor_bg = coral;
  theme.tree_cursor_fg = near_black;
  theme.track_cursor_bg = crimson;
  theme.track_cursor_fg = near_black;
  theme.active_collection = crimson;
  theme.playing = coral;
  theme.tree_item = muted_red;
  theme.hover_bg = deep_black;
  theme.hover_border = coral;
  theme.visual_selection_bg = wine;
  theme.visual_cursor_bg = crimson;
  theme.progress_filled = crimson;
  theme.progress_filled_end = coral;
  theme.progress_empty = deep_black;
  theme.progress_knob = coral;
  theme.volume_fill = coral;
  theme.volume_empty = deep_black;
  theme.icon = warm_white;
  theme.spectrum_low = wine;
  theme.spectrum_high = crimson;
  theme.spectrum_peak = coral;
  theme.success = warm_white;
  theme.warning = coral;
  theme.error = crimson;
  theme.info = pale_border;
  theme.ok = warm_white;
  theme.input_border = pale_border;
  return theme;
}

} // namespace

ThemeRegistry::ThemeRegistry() {
  // Catppuccin Mocha is the application default, and it is registered FIRST:
  // an unknown or legacy theme id (including the historical "default")
  // resolves to entries_.front(), so existing configuration keeps working and
  // picks up the new palette.
  registerTheme("catppuccin-mocha", "Catppuccin Mocha", Theme{});
  // The rest of the built-in list. The ORDER here is the order the
  // Appearance -> Theme choice offers, so it runs from the default through the
  // remaining presets to the second Catppuccin flavour.
  registerTheme("kanagawa", "Kanagawa", kanagawaTheme());
  registerTheme("material-palenight", "Material Palenight",
                materialPalenightTheme());
  registerTheme("monokai-pro", "Monokai Pro", monokaiProTheme());
  registerTheme("github-dark", "GitHub Dark", githubDarkTheme());
  registerTheme("oxocarbon", "Oxocarbon", oxocarbonTheme());
  registerTheme("crimson", "Crimson", crimsonTheme());
  registerTheme("catppuccin-macchiato", "Catppuccin Macchiato",
                catppuccinMacchiatoTheme());
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
