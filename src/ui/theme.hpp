#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/screen/color.hpp>

namespace termusic::ui {

/// Every colour used by the UI. Views must never hard-code a colour.
///
/// The palette is Catppuccin Mocha: each role below carries the OFFICIAL
/// palette value it maps to, so re-theming means editing this table rather
/// than hunting for RGB literals in the renderers. Semantic roles stay stable
/// even when the palette behind them changes.
///
/// Distribution is deliberately restrained: most of the screen is
/// Base/Mantle/Text/Surface, Mauve is the single primary accent, Pink /
/// Sapphire / Sky / Teal are secondary, and Green / Yellow / Red are reserved
/// for success / warning / error.
struct Theme {
  // --- Surfaces -------------------------------------------------------------
  ftxui::Color background = ftxui::Color::RGB(0x1E, 0x1E, 0x2E);      // Base
  ftxui::Color background_deep = ftxui::Color::RGB(0x11, 0x11, 0x1B); // Crust
  ftxui::Color panel = ftxui::Color::RGB(0x18, 0x18, 0x25);           // Mantle
  ftxui::Color surface = ftxui::Color::RGB(0x31, 0x32, 0x44);         // Surface0

  // --- Text -----------------------------------------------------------------
  ftxui::Color text = ftxui::Color::RGB(0xCD, 0xD6, 0xF4);        // Text
  ftxui::Color header_text = ftxui::Color::RGB(0xBA, 0xC2, 0xDE); // Subtext1
  ftxui::Color muted_text = ftxui::Color::RGB(0xA6, 0xAD, 0xC8);  // Subtext0
  ftxui::Color weak_text = ftxui::Color::RGB(0x7F, 0x84, 0x9C);   // Overlay1

  // --- Lines ----------------------------------------------------------------
  /// Outer panel frame: visible, but never competing with content.
  ftxui::Color border = ftxui::Color::RGB(0x45, 0x47, 0x5A); // Surface1
  /// Low-contrast internal rule (table headers, separators inside a panel).
  ftxui::Color border_dim = ftxui::Color::RGB(0x31, 0x32, 0x44); // Surface0
  /// The top-bar separator. Softer than the outer frame on purpose.
  ftxui::Color divider = ftxui::Color::RGB(0x45, 0x47, 0x5A);   // Surface1
  ftxui::Color separator = ftxui::Color::RGB(0x45, 0x47, 0x5A); // Surface1

  // --- Accents --------------------------------------------------------------
  /// The primary UI accent: active page, panel titles, prompts.
  ftxui::Color accent_primary = ftxui::Color::RGB(0xCB, 0xA6, 0xF7); // Mauve
  /// The secondary accent: brand mark, informational highlights.
  ftxui::Color accent_secondary = ftxui::Color::RGB(0x74, 0xC7, 0xEC); // Sapphire
  /// Tertiary accent for "user-defined" states.
  ftxui::Color accent_purple = ftxui::Color::RGB(0xB4, 0xBE, 0xFE); // Lavender
  /// The `termusic` wordmark. Its own role so the brand never has to borrow
  /// the focus colour.
  ftxui::Color brand = ftxui::Color::RGB(0xF5, 0xC2, 0xE7); // Pink
  /// The immersive mode's frame. Deliberately BRIGHT: the now-playing view is
  /// framed like a card, unlike the workspace's quiet Surface border.
  ftxui::Color frame = ftxui::Color::RGB(0xCD, 0xD6, 0xF4); // Text (near white)

  // --- Focus / selection ----------------------------------------------------
  /// Generic list selection (Settings menus, keybinding rows).
  ftxui::Color selected_fg = ftxui::Color::RGB(0x11, 0x11, 0x1B); // Crust
  ftxui::Color selected_bg = ftxui::Color::RGB(0xCB, 0xA6, 0xF7); // Mauve
  /// Workspace Tree cursor: PINK, deliberately distinct from the Track cursor.
  ftxui::Color tree_cursor_bg = ftxui::Color::RGB(0xF5, 0xC2, 0xE7); // Pink
  ftxui::Color tree_cursor_fg = ftxui::Color::RGB(0x11, 0x11, 0x1B); // Crust
  /// Track Buffer cursor: MAUVE.
  ftxui::Color track_cursor_bg = ftxui::Color::RGB(0xCB, 0xA6, 0xF7); // Mauve
  ftxui::Color track_cursor_fg = ftxui::Color::RGB(0x11, 0x11, 0x1B); // Crust
  /// The open collection's ROW COLOUR. The tree marks it with colour and text
  /// weight only -- no dot, no ball, no symbol -- and a different blue from
  /// `playing`, so the active collection and the current song never read as the
  /// same state.
  ftxui::Color active_collection = ftxui::Color::RGB(0x74, 0xC7, 0xEC); // Sapphire
  /// The track MPD is playing (▶). Never fills a whole row.
  ftxui::Color playing = ftxui::Color::RGB(0x89, 0xDC, 0xEB); // Sky
  /// Tree rows INSIDE the two roots. Lighter than the roots' magenta so the
  /// hierarchy reads at a glance: magenta directory, pink-purple contents.
  ftxui::Color tree_item = ftxui::Color::RGB(0xF5, 0xC2, 0xE7); // Pink

  ftxui::Color hover_bg = ftxui::Color::RGB(0x31, 0x32, 0x44);     // Surface0
  ftxui::Color hover_border = ftxui::Color::RGB(0x6C, 0x70, 0x86); // Overlay0
  /// Visual range: a restrained wash that never competes with the cursor row
  /// or the playing marker.
  ftxui::Color visual_selection_bg =
      ftxui::Color::RGB(0x45, 0x47, 0x5A); // Surface1
  /// Visual mode's moving cursor keeps the strong Mauve highlight.
  ftxui::Color visual_cursor_bg = ftxui::Color::RGB(0xCB, 0xA6, 0xF7); // Mauve

  // --- Progress / volume ----------------------------------------------------
  ftxui::Color progress_filled = ftxui::Color::RGB(0xCB, 0xA6, 0xF7); // Mauve
  ftxui::Color progress_filled_end =
      ftxui::Color::RGB(0xCB, 0xA6, 0xF7);                             // Mauve
  ftxui::Color progress_empty = ftxui::Color::RGB(0x31, 0x32, 0x44);   // Surface0
  ftxui::Color progress_knob = ftxui::Color::RGB(0xB4, 0xBE, 0xFE);    // Lavender
  /// The volume slider is its own scale, so it gets its own colour.
  ftxui::Color volume_fill = ftxui::Color::RGB(0x94, 0xE2, 0xD5);  // Teal
  ftxui::Color volume_empty = ftxui::Color::RGB(0x31, 0x32, 0x44); // Surface0
  /// Resting colour for player glyphs.
  ftxui::Color icon = ftxui::Color::RGB(0xBA, 0xC2, 0xDE); // Subtext1

  // --- Spectrum -------------------------------------------------------------
  /// Two accents only: the spectrum is not a rainbow.
  ftxui::Color spectrum_low = ftxui::Color::RGB(0xCB, 0xA6, 0xF7);  // Mauve
  ftxui::Color spectrum_high = ftxui::Color::RGB(0xF5, 0xC2, 0xE7); // Pink
  ftxui::Color spectrum_peak = ftxui::Color::RGB(0xF5, 0xC2, 0xE7); // Pink

  // --- Status ---------------------------------------------------------------
  ftxui::Color success = ftxui::Color::RGB(0xA6, 0xE3, 0xA1); // Green
  ftxui::Color warning = ftxui::Color::RGB(0xF9, 0xE2, 0xAF); // Yellow
  ftxui::Color error = ftxui::Color::RGB(0xF3, 0x8B, 0xA8);   // Red
  ftxui::Color info = ftxui::Color::RGB(0x74, 0xC7, 0xEC);    // Sapphire
  /// Kept as an alias of `success` for existing theme files.
  ftxui::Color ok = ftxui::Color::RGB(0xA6, 0xE3, 0xA1); // Green

  /// The inline input box's frame. This is the ONE deliberately non-palette
  /// colour in the theme: the box must read as an input CONTROL rather than as
  /// another Surface1 panel border, so it is drawn in plain white.
  ftxui::Color input_border = ftxui::Color::White;
};

struct ThemeInfo {
  std::string id;
  std::string name;
  std::filesystem::path source;
};

/// Owns built-in and file-backed themes. Theme IDs are stable configuration
/// values; display names are free to change.
class ThemeRegistry {
public:
  ThemeRegistry();

  bool registerTheme(std::string id, std::string name, Theme theme,
                     std::filesystem::path source = {},
                     std::string *error = nullptr);
  bool loadFile(const std::filesystem::path &path,
                std::string *error = nullptr);
  std::size_t loadDirectory(const std::filesystem::path &directory,
                            std::vector<std::string> *warnings = nullptr);

  const Theme &resolve(std::string_view id) const;
  std::string resolveId(std::string_view id) const;
  std::string nextId(std::string_view current) const;
  std::vector<ThemeInfo> list() const;

private:
  struct Entry {
    ThemeInfo info;
    Theme theme;
  };
  std::vector<Entry> entries_;
};

/// Kept as a small compatibility helper for widgets and downstream users.
const Theme &defaultTheme();

} // namespace termusic::ui
