#pragma once

#include <string>
#include <vector>

#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>

#include "ui/theme.hpp"

namespace termusic::ui {

/// Every glyph the UI draws, in one place. Each comes from a single icon
/// family so stroke weight, optical size and baseline match; mixing unrelated
/// Unicode symbols (and emoji) is what made the previous bar look inconsistent.
///
/// The first group is the transport; the second is CONTENT SEMANTICS for the
/// Vault tree and the track rows. A content icon says what a row IS (a folder,
/// a song, a saved list) -- never what state it is in, which is what the
/// existing tree indicator, the active-collection dot and the playing marker
/// are for.
enum class Icon {
  // Transport.
  Shuffle,
  Previous,
  Play,
  Pause,
  Next,
  Repeat,
  Speaker,
  // Content semantics.
  Folder,      ///< a closed directory
  FolderOpen,  ///< a directory that is expanded
  Library,     ///< the media database
  History,     ///< the played history
  Playlist,    ///< a saved playlist
  Music,       ///< a song row
  /// A `core` entry that only SHOWS something: text, documentation, help.
  Document,
  /// A `core` entry that can be CHANGED: a settings module.
  Settings,
};

/// True for the content-semantics icons, which are drawn at a fixed width in
/// front of a label or a title. Keeping the answer here means a row renderer
/// never has to guess how much space an icon takes.
bool isContentIcon(Icon icon);

/// The cell width every content icon occupies, including the space that
/// separates it from the text after it. One constant, so the tree and the
/// track table cannot disagree about the column they reserve.
constexpr int kContentIconWidth = 2;

/// Which family to draw from.
///
/// * `NerdFont`  - Font Awesome glyphs from the Nerd Font private use area.
///   One family, uniform weight; requires a Nerd Font terminal font.
/// * `Unicode`   - geometric fallback for terminals without a Nerd Font.
enum class IconSet {
  NerdFont,
  Unicode,
};

IconSet iconSetFromName(std::string_view name);
const char *iconSetName(IconSet set);

/// The single glyph for an icon.
std::string iconGlyph(Icon icon, IconSet set);

/// A transport button: a fixed hit box with the icon centred inside it, and an
/// optional magenta focus ring.
///
/// `box_width`/`box_height` come from `UiMetrics`.
/// `toggle` marks a stateful control (shuffle/repeat): when it is OFF it stays
/// dimmer than an ordinary transport button, so "available" and "enabled" are
/// distinguishable at a glance.
ftxui::Element transportButton(Icon icon, IconSet set, const Theme &theme,
                              bool focused, bool highlighted, int box_width,
                              int box_height, bool hovered = false,
                              bool toggle = false);

/// The single coordinate model shared by rendering, mouse hit-testing and
/// seeking. Nothing else may re-derive a thumb position from `progress`.
struct SliderGeometry {
  int track_cells = 1;
  double progress = 0.0;
  /// progress * (track_cells - 1): the one authoritative float position.
  /// NOTE: never progress * track_cells -- that is the off-by-one that put the
  /// fill one cell past the thumb.
  double exact_position = 0.0;
  /// The cell the thumb occupies. Always within [0, track_cells - 1].
  int thumb_cell = 0;
  /// Cells strictly before the thumb: the fill always ends flush with it.
  int full_cells = 0;
  /// Sub-cell remainder of exact_position; 0 for whole positions.
  double fraction = 0.0;

  static SliderGeometry fromProgress(int track_cells, double progress);
  /// Inverse mapping used by mouse drag: a cell offset back to 0..1.
  static double progressFromPosition(int track_cells, double position);
};

/// Per-cell classification. The renderers iterate exactly this vector, so the
/// emitted cell count is provably `track_cells` and a test can assert it
/// without having to measure a rendered Element.
enum class SliderCellKind {
  Filled,
  Thumb,
  Empty,
};

std::vector<SliderCellKind> sliderCells(const SliderGeometry &geometry);

/// Which sub-cell technique draws the capsule. Selectable so the three
/// candidates can be compared side by side in a real terminal.
enum class SliderRenderer {
  /// A: thin continuous straight line, drawn as braille sub-cell dots.
  Line,
  /// B: same braille line with a very low-amplitude, fixed-phase sine. This is
  /// the default: braille gives 2x4 sub-cell resolution, so no cell grid is
  /// visible, and the wave reads as a calm track rather than a spectrum.
  Wave,
  /// C: the FTXUI Canvas braille path. Currently renders blank on this build;
  /// kept only so the comparison can be reproduced.
  Braille,
  /// Compatibility fallback: half-block track (visible cell segmentation).
  HalfBlock,
  /// Compatibility fallback: full-cell background fill.
  Background,
};

SliderRenderer sliderRendererFromName(std::string_view name);
const char *sliderRendererName(SliderRenderer renderer);

/// Continuous capsule slider used by both the progress and volume controls.
///
/// This is deliberately NOT the visualizer's cell renderer (requirements §28):
/// the track is a flat, seamlessly tiling half-block band with a single colour,
/// and the thumb is a full-height circle, so no per-cell grid is visible.
ftxui::Element continuousSlider(double ratio, int width, const Theme &theme,
                                SliderRenderer renderer);

/// Braille sub-cell renderer. `wave` selects the low-amplitude sine; both
/// variants consume the frozen SliderGeometry, so thumb placement and cell
/// count are unchanged from the half-block implementation.
ftxui::Element brailleTrack(const SliderGeometry &geometry, const Theme &theme,
                            bool wave, int columns);

/// Speaker drawn from explicit geometry in the primary font's block range, so
/// it never depends on a fallback icon font.
ftxui::Element geometricSpeaker(const Theme &theme);

/// Helper used by the player bar for the focus rectangle.
std::string focusBoxTop(int width);
std::string focusBoxMiddle(int width);
std::string focusBoxBottom(int width);

/// The one button chrome used by every settings control (and by the playlist
/// prompts): a label with a single leading and trailing space, inverted while
/// it holds focus. Shared so two panes can never drift apart visually.
ftxui::ButtonOption compactButton(const Theme &theme);

/// The one list chrome.
///
/// `horizontal` selects the top-navigation look (accent + underline for the
/// active entry); `navigation` marks a selector whose entries are views rather
/// than data rows. Default is a data list: a solid band for the active row and
/// a faint wash for the focused one.
ftxui::MenuOption styledMenu(const Theme &theme, bool horizontal = false,
                             bool navigation = false);

} // namespace termusic::ui
