#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "util/text.hpp"

namespace termusic::ui {
namespace {

/// Font Awesome glyphs from the Nerd Font private use area. All seven icons
/// come from that one family, so they share stroke weight and optical size.
constexpr const char *kNerdShuffle = "\uf074";  // fa-random
constexpr const char *kNerdPrevious = "\uf048"; // fa-step-backward
constexpr const char *kNerdPlay = "\uf04b";     // fa-play
constexpr const char *kNerdPause = "\uf04c";    // fa-pause
constexpr const char *kNerdNext = "\uf051";     // fa-step-forward
constexpr const char *kNerdRepeat = "\uf01e";   // fa-repeat
constexpr const char *kNerdSpeaker = "\uf028";  // fa-volume-up
// Content semantics, same family (Font Awesome): a folder, an open folder, a
// music note, a clock-with-arrow, a bulleted list.
constexpr const char *kNerdFolder = "\uf07b";     // fa-folder
constexpr const char *kNerdFolderOpen = "\uf07c"; // fa-folder-open
constexpr const char *kNerdMusic = "\uf001";      // fa-music
constexpr const char *kNerdHistory = "\uf1da";    // fa-history
constexpr const char *kNerdStream = "\uf1eb";     // fa-wifi
constexpr const char *kNerdAgent = "\uf0d0";      // fa-magic
constexpr const char *kNerdPlaylist = "\uf0ca";   // fa-list-ul
// The two `core` entry kinds. Same family again: a text page and a cog, so the
// classification reads as "information" versus "configurable".
constexpr const char *kNerdDocument = "\uf15c"; // fa-file-text-o
constexpr const char *kNerdSettings = "\uf013"; // fa-cog

// Geometric fallback, only used when the terminal has no Nerd Font. Kept as
// close to one weight as the base Unicode blocks allow.
constexpr const char *kUniShuffle = "\u21c4";
constexpr const char *kUniPrevious = "\u25c0\u258f";
constexpr const char *kUniPlay = "\u25b6";
constexpr const char *kUniPause = "\u275a\u275a";
constexpr const char *kUniNext = "\u258f\u25b6";
constexpr const char *kUniRepeat = "\u27f3";
constexpr const char *kUniSpeaker = "\u25b6\u25cf";
// Width-safe Unicode fallbacks. Deliberately NOT emoji: an emoji's width and
// baseline vary between terminals, which would break the columns an icon sits
// in front of.
constexpr const char *kUniFolder =
    "\u25a3"; // white square containing black small square
constexpr const char *kUniFolderOpen =
    "\u25a2";                               // white square with rounded corners
constexpr const char *kUniMusic = "\u266a"; // quaver
constexpr const char *kUniHistory = "\u21ba";  // anticlockwise arrow
constexpr const char *kUniStream = "\u223f";   // sine wave
constexpr const char *kUniAgent = "\u2726";    // four pointed star
constexpr const char *kUniPlaylist = "\u2261"; // identical to (a list)
// Width-safe stand-ins: a ruled page and a heavy asterisk. Deliberately NOT
// the gear/emoji codepoints (U+2699 and friends), whose width and presentation
// vary between terminals and would move the column they sit in.
constexpr const char *kUniDocument = "\u25a4"; // square with horizontal fill
constexpr const char *kUniSettings = "\u2731"; // heavy asterisk

} // namespace

IconSet iconSetFromName(std::string_view name) {
  if (name == "unicode" || name == "fallback")
    return IconSet::Unicode;
  return IconSet::NerdFont;
}

const char *iconSetName(IconSet set) {
  return set == IconSet::NerdFont ? "nerd" : "unicode";
}

std::string iconGlyph(Icon icon, IconSet set) {
  const bool nerd = set == IconSet::NerdFont;
  switch (icon) {
  case Icon::Shuffle:
    return nerd ? kNerdShuffle : kUniShuffle;
  case Icon::Previous:
    return nerd ? kNerdPrevious : kUniPrevious;
  case Icon::Play:
    return nerd ? kNerdPlay : kUniPlay;
  case Icon::Pause:
    return nerd ? kNerdPause : kUniPause;
  case Icon::Next:
    return nerd ? kNerdNext : kUniNext;
  case Icon::Repeat:
    return nerd ? kNerdRepeat : kUniRepeat;
  case Icon::Speaker:
    return nerd ? kNerdSpeaker : kUniSpeaker;
  case Icon::Folder:
    return nerd ? kNerdFolder : kUniFolder;
  case Icon::FolderOpen:
    return nerd ? kNerdFolderOpen : kUniFolderOpen;
  case Icon::Library:
    return nerd ? kNerdMusic : kUniMusic;
  case Icon::History:
    return nerd ? kNerdHistory : kUniHistory;
  case Icon::Stream:
    return nerd ? kNerdStream : kUniStream;
  case Icon::Agent:
    return nerd ? kNerdAgent : kUniAgent;
  case Icon::Playlist:
    return nerd ? kNerdPlaylist : kUniPlaylist;
  case Icon::Music:
    return nerd ? kNerdMusic : kUniMusic;
  case Icon::Document:
    return nerd ? kNerdDocument : kUniDocument;
  case Icon::Settings:
    return nerd ? kNerdSettings : kUniSettings;
  }
  return "?";
}

bool isContentIcon(Icon icon) {
  switch (icon) {
  case Icon::Folder:
  case Icon::FolderOpen:
  case Icon::Library:
  case Icon::History:
  case Icon::Stream:
  case Icon::Agent:
  case Icon::Playlist:
  case Icon::Music:
  case Icon::Document:
  case Icon::Settings:
    return true;
  case Icon::Shuffle:
  case Icon::Previous:
  case Icon::Play:
  case Icon::Pause:
  case Icon::Next:
  case Icon::Repeat:
  case Icon::Speaker:
    return false;
  }
  return false;
}

std::string focusBoxTop(int width) {
  return "\u256d" + util::repeat(std::max(0, width - 2), "\u2500") + "\u256e";
}

std::string focusBoxMiddle(int width) {
  (void)width;
  return "\u2502";
}

std::string focusBoxBottom(int width) {
  return "\u2570" + util::repeat(std::max(0, width - 2), "\u2500") + "\u256f";
}

ftxui::Element transportButton(Icon icon, IconSet set, const Theme &theme,
                               bool focused, bool highlighted, int box_width,
                               int box_height, bool hovered, bool toggle) {
  const std::string glyph = iconGlyph(icon, set);
  // No focus rectangle, no pressed box, no pill. State is communicated by
  // colour alone, so gaining focus or pressing can never redraw chrome or
  // shift a neighbour:
  //   normal  -> Subtext1         hover     -> Text
  //   toggled on -> Mauve         toggled off -> Overlay1 (dimmer)
  const ftxui::Color tint = highlighted ? theme.accent_primary
                            : hovered   ? theme.text
                            : toggle    ? theme.weak_text
                                        : theme.icon;
  (void)focused;
  const int width = std::max(1, box_width);
  const int height = std::max(1, box_height);
  const int glyph_width = std::max(1, util::displayWidth(glyph));
  const int slack = std::max(0, width - glyph_width);
  const int left = slack / 2;
  const int right = slack - left;

  const ftxui::Element label =
      ftxui::text(util::repeat(left, " ") + glyph + util::repeat(right, " ")) |
      ftxui::color(tint) | ftxui::bold;

  // Always exactly box_width x box_height: Play <-> Pause and focus changes
  // never alter geometry.
  if (height <= 1) {
    return label;
  }
  ftxui::Elements rows;
  const int pad_top = (height - 1) / 2;
  const int pad_bottom = height - 1 - pad_top;
  for (int index = 0; index < pad_top; ++index)
    rows.push_back(ftxui::text(""));
  rows.push_back(label);
  for (int index = 0; index < pad_bottom; ++index)
    rows.push_back(ftxui::text(""));
  return ftxui::vbox(std::move(rows)) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
}

SliderGeometry SliderGeometry::fromProgress(int track_cells, double progress) {
  SliderGeometry geometry;
  geometry.track_cells = std::max(1, track_cells);
  geometry.progress = std::clamp(progress, 0.0, 1.0);
  geometry.exact_position =
      geometry.progress * static_cast<double>(geometry.track_cells - 1);
  geometry.thumb_cell =
      std::clamp(static_cast<int>(std::lround(geometry.exact_position)), 0,
                 geometry.track_cells - 1);
  geometry.full_cells = geometry.thumb_cell;
  geometry.fraction =
      geometry.exact_position - std::floor(geometry.exact_position);
  return geometry;
}

double SliderGeometry::progressFromPosition(int track_cells, double position) {
  const int cells = std::max(1, track_cells);
  if (cells <= 1)
    return 0.0;
  return std::clamp(position / static_cast<double>(cells - 1), 0.0, 1.0);
}

std::vector<SliderCellKind> sliderCells(const SliderGeometry &geometry) {
  const int cells = std::max(1, geometry.track_cells);
  const int thumb = std::clamp(geometry.thumb_cell, 0, cells - 1);
  std::vector<SliderCellKind> kinds;
  kinds.reserve(static_cast<std::size_t>(cells));
  for (int index = 0; index < cells; ++index) {
    if (index == thumb)
      kinds.push_back(SliderCellKind::Thumb);
    else if (index < thumb)
      kinds.push_back(SliderCellKind::Filled);
    else
      kinds.push_back(SliderCellKind::Empty);
  }
  return kinds;
}

SliderRenderer sliderRendererFromName(std::string_view name) {
  if (name == "line" || name == "straight")
    return SliderRenderer::Line;
  if (name == "wave")
    return SliderRenderer::Wave;
  if (name == "bg" || name == "background")
    return SliderRenderer::Background;
  if (name == "braille" || name == "canvas")
    return SliderRenderer::Braille;
  return SliderRenderer::HalfBlock;
}

const char *sliderRendererName(SliderRenderer renderer) {
  switch (renderer) {
  case SliderRenderer::Line:
    return "A:braille-line";
  case SliderRenderer::Wave:
    return "B:braille-wave";
  case SliderRenderer::Braille:
    return "C:canvas-braille";
  case SliderRenderer::HalfBlock:
    return "D:half-block";
  case SliderRenderer::Background:
    return "E:background";
  }
  return "B:braille-wave";
}

namespace {

/// Half-block track with a fractional leading edge. The thumb REPLACES its
/// cell; nothing is ever appended.
ftxui::Element halfBlockSlider(double ratio, int width, const Theme &theme) {
  const int cells = std::max(1, width);
  const SliderGeometry geometry = SliderGeometry::fromProgress(cells, ratio);
  ftxui::Elements out;
  out.reserve(static_cast<std::size_t>(cells));
  for (const SliderCellKind kind : sliderCells(geometry)) {
    switch (kind) {
    case SliderCellKind::Thumb:
      out.push_back(ftxui::text("\u25cf") | ftxui::color(theme.progress_knob) |
                    ftxui::bold);
      break;
    case SliderCellKind::Filled:
      out.push_back(ftxui::text("\u2584") |
                    ftxui::color(theme.progress_filled));
      break;
    case SliderCellKind::Empty:
      out.push_back(ftxui::text("\u2584") | ftxui::color(theme.progress_empty));
      break;
    }
  }
  return ftxui::hbox(std::move(out));
}

ftxui::Element backgroundSlider(double ratio, int width, const Theme &theme) {
  const int cells = std::max(1, width);
  const SliderGeometry geometry = SliderGeometry::fromProgress(cells, ratio);
  ftxui::Elements out;
  out.reserve(static_cast<std::size_t>(cells));
  for (const SliderCellKind kind : sliderCells(geometry)) {
    const bool played = kind != SliderCellKind::Empty;
    out.push_back(
        ftxui::text(kind == SliderCellKind::Thumb ? "\u25cf" : " ") |
        ftxui::color(theme.progress_knob) |
        ftxui::bgcolor(played ? theme.progress_filled : theme.progress_empty));
  }
  return ftxui::hbox(std::move(out));
}

ftxui::Element brailleSlider(double ratio, int width, const Theme &theme) {
  const int cells = std::max(1, width);
  const SliderGeometry geometry = SliderGeometry::fromProgress(cells, ratio);
  ftxui::Canvas canvas(cells, 1);
  const int dots_x = cells * 2;
  const int fill = std::clamp(geometry.thumb_cell * 2 + 1, 0, dots_x - 1);
  for (int x = 0; x < dots_x; ++x) {
    const ftxui::Color color =
        x <= fill ? theme.progress_filled : theme.progress_empty;
    canvas.DrawPoint(x, 1, true, color);
    canvas.DrawPoint(x, 2, true, color);
  }
  canvas.DrawPointCircleFilled(fill, 2, 1, theme.progress_knob);
  return ftxui::canvas(std::move(canvas));
}

} // namespace

namespace {
constexpr int kBrailleBit[2][4] = {
    {0x01, 0x02, 0x04, 0x40},
    {0x08, 0x10, 0x20, 0x80},
};

std::string brailleGlyph(int mask) {
  const unsigned int codepoint = 0x2800U + static_cast<unsigned int>(mask);
  std::string out;
  out.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
  out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
  out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  return out;
}
} // namespace

ftxui::Element brailleTrack(const SliderGeometry &geometry, const Theme &theme,
                            bool wave, int columns) {
  const int cells = std::max(1, geometry.track_cells);
  const auto kinds = sliderCells(geometry);
  const double dots = static_cast<double>(cells) * 2.0;
  const double wavelength = std::max(8.0, dots / 4.5);
  constexpr double kTwoPi = 6.283185307179586;
  constexpr double kBaseline = 1.5;
  const double amplitude = wave ? 0.75 : 0.0;
  constexpr int kSuperSamples = 8;

  ftxui::Elements out;
  out.reserve(static_cast<std::size_t>(cells));
  for (int cell = 0; cell < cells; ++cell) {
    if (kinds[static_cast<std::size_t>(cell)] == SliderCellKind::Thumb) {
      const double x = static_cast<double>(cell) * 2.0 + 1.0;
      const double y =
          kBaseline + amplitude * std::sin(x * kTwoPi / wavelength);
      const int row = std::clamp(static_cast<int>(std::lround(y)), 0, 3);
      int mask = kBrailleBit[0][row] | kBrailleBit[1][row];
      const int lower = std::min(3, row + 1);
      mask |= kBrailleBit[0][lower] | kBrailleBit[1][lower];
      out.push_back(ftxui::text(brailleGlyph(mask)) |
                    ftxui::color(theme.progress_knob) | ftxui::bold);
      continue;
    }
    int mask = 0;
    for (int sub = 0; sub < 2; ++sub) {
      double sum = 0.0;
      for (int sample = 0; sample < kSuperSamples; ++sample) {
        const double x = static_cast<double>(cell) * 2.0 +
                         static_cast<double>(sub) +
                         (static_cast<double>(sample) + 0.5) /
                             static_cast<double>(kSuperSamples);
        sum += kBaseline + amplitude * std::sin(x * kTwoPi / wavelength);
      }
      const double y = sum / static_cast<double>(kSuperSamples);
      const int row = std::clamp(static_cast<int>(std::lround(y)), 0, 3);
      mask |= kBrailleBit[sub][row];
    }
    const bool played =
        kinds[static_cast<std::size_t>(cell)] == SliderCellKind::Filled;
    out.push_back(
        ftxui::text(brailleGlyph(mask)) |
        ftxui::color(played ? theme.progress_filled : theme.progress_empty));
  }
  (void)columns;
  return ftxui::hbox(std::move(out));
}

ftxui::Element continuousSlider(double ratio, int width, const Theme &theme,
                                SliderRenderer renderer) {
  const int cells = std::max(1, width);
  const SliderGeometry geometry = SliderGeometry::fromProgress(cells, ratio);
  switch (renderer) {
  case SliderRenderer::Line:
    return brailleTrack(geometry, theme, false, cells);
  case SliderRenderer::Wave:
    return brailleTrack(geometry, theme, true, cells);
  case SliderRenderer::Braille:
    return brailleSlider(ratio, cells, theme);
  case SliderRenderer::HalfBlock:
    return halfBlockSlider(ratio, cells, theme);
  case SliderRenderer::Background:
    return backgroundSlider(ratio, cells, theme);
  }
  return halfBlockSlider(ratio, cells, theme);
}

ftxui::Element geometricSpeaker(const Theme &theme) {
  // Speaker body plus two waves, assembled from block glyphs the primary font
  // already provides. Keeps the whole player bar on one font.
  ftxui::Elements body;
  body.push_back(ftxui::text("\u2588") | ftxui::color(theme.icon));
  body.push_back(ftxui::text("\u2590") | ftxui::color(theme.icon));
  ftxui::Elements waves;
  waves.push_back(ftxui::text("\u2590") | ftxui::color(theme.icon));
  waves.push_back(ftxui::text("\u2590") | ftxui::color(theme.icon));
  return ftxui::hbox({
      ftxui::vbox(
          {ftxui::text(" "), ftxui::hbox(std::move(body)), ftxui::text(" ")}),
      ftxui::vbox(
          {ftxui::text("\u2584"), ftxui::text(" "), ftxui::text("\u2580")}) |
          ftxui::color(theme.icon),
      ftxui::vbox({ftxui::text(" "), ftxui::text("\u2590"), ftxui::text(" ")}) |
          ftxui::color(theme.icon),
  });
}

} // namespace termusic::ui

namespace termusic::ui {

using namespace ftxui;

ButtonOption compactButton(const Theme &theme) {
  ButtonOption option = ButtonOption::Simple();
  option.transform = [&theme](const EntryState &state) {
    Element entry = text(" " + state.label + " ");
    if (state.focused || state.active) {
      return entry | color(theme.selected_fg) | bgcolor(theme.selected_bg) |
             bold;
    }
    return entry | color(theme.text);
  };
  return option;
}

MenuOption styledMenu(const Theme &theme, bool horizontal, bool navigation) {
  MenuOption option =
      horizontal ? MenuOption::Horizontal() : MenuOption::Vertical();
  option.entries_option.transform = [&theme,
                                     navigation](const EntryState &state) {
    Element entry = text(state.label);
    if (navigation) {
      // Top navigation: the active tab is magenta with a short underline.
      if (state.active)
        entry = entry | color(theme.accent_primary) | bold | underlined;
      else if (state.focused)
        entry = entry | color(theme.accent_secondary) | bold;
      else
        entry = entry | color(theme.muted_text);
      return entry;
    }
    // Data rows: the active row is a solid magenta band with dark text, a
    // hovered row only gets a faint wash.
    if (state.active)
      return entry | bold | color(theme.selected_fg) |
             bgcolor(theme.selected_bg);
    if (state.focused)
      return entry | bold | color(theme.text) | bgcolor(theme.hover_bg);
    return entry | bold | color(theme.text);
  };
  return option;
}

} // namespace termusic::ui
