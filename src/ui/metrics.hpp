#pragma once

#include <algorithm>
#include <cmath>

namespace termusic::ui {

/// How many small rectangles a frequency column shows for one amplitude.
///
/// The single rule the whole grid obeys:
///   * silence (or anything below the threshold) is ZERO blocks -- never a
///     decorative floor, never a dotted baseline;
///   * a live but very quiet band is ONE block, so a real signal is always
///     visible without inventing movement;
///   * the rest scales linearly, and amplitude above 1 clamps, so a hot signal
///     can never draw outside the grid.
inline int visualizerBlockCount(float amplitude, int levels) {
  if (levels <= 0 || !(amplitude > 0.02F))
    return 0;
  const int blocks = static_cast<int>(std::lround(
      static_cast<double>(amplitude) * static_cast<double>(levels)));
  return std::clamp(blocks, 1, levels);
}

/// Responsive buckets. The reference layout is reproduced in `Large`; each
/// smaller bucket drops non-essential chrome instead of refusing to draw.
enum class LayoutMode {
  Large,
  Medium,
  Compact,
  Minimal,
};

/// Widths of the Track Buffer's columns, in terminal cells. A width of 0 means
/// the column is not shown at all, which is how narrow terminals stay readable
/// instead of squeezing every field.
///
/// `title_lead` is a separating space that belongs to the TITLE column: it is
/// held separately so that a row whose artist is empty still starts its title
/// at the same cell as every other row.
struct TrackColumns {
  int index = 0;
  /// The music icon in front of the title, plus its separating space. It is a
  /// real column, budgeted like every other one, so adding it can never push
  /// the artist or the album past the panel edge.
  int icon = 0;
  int title_lead = 0;
  int title = 0;
  int artist = 0;
  int album = 0;
  int duration = 0;
  int size = 0;
  int played = 0;

  /// Cells this budget occupies, excluding the two-cell playing marker and the
  /// space after it (the renderer charges those separately). Used by tests to
  /// prove a row can never be wider than the panel.
  int total() const {
    return index + icon + title_lead + title + artist + album + duration +
           size + played;
  }
};

/// Geometry of the immersive now-playing body.
///
/// The structure is a single centered column, and every band and gap is sized
/// here so the renderer never invents a position:
///
///   +-------------------------------- body --------------------------------+
///   |                      title (centered, heavier)                        |  info_rows
///   |                      artist (centered, lighter)                       |
///   |                                                                       |  info_gap_rows
///   |                                                                       |  visualizer_top_pad
///   |                 visualizer (the primary content)                      |  visualizer_rows
///   |                                                                       |  visualizer_bottom_pad
///   +-----------------------------------------------------------------------+
///                                                                              player_gap_rows
///   ------------------------------ Player Bar ------------------------------
///
/// The screen is the song's identity and its sound, and nothing else: no
/// lyrics, no metadata block, and no artwork -- so nothing that might be
/// missing can move a single cell of this layout.
///
/// Both logical separations -- information to visualizer, and visualizer to the
/// Player Bar -- are SPACE. Nothing is drawn in either of them: no rule, no
/// border, no separator glyph.
///
/// Properties this struct exists to guarantee:
///
///  * the information bar is at the TOP and centered across the CONTENT area
///    (between the outer margins), so nothing below it can move it;
///  * the visualizer gets the entire content width and the whole visual region
///    between those gaps, minus a restrained top/bottom pad, so it reads as one
///    large centered element rather than a panel beside something else;
///  * every row and column adds up exactly, so no band can overflow the frame.
struct ImmersiveLayout {
  int body_width = 0;
  int body_height = 0;

  /// Blank cells between the frame and the content, on both sides. This is the
  /// visualizer's horizontal breathing room.
  int outer_left_pad = 0;
  int outer_right_pad = 0;
  /// Width available to the bands once both margins are taken out.
  int content_width = 0;

  /// Title over artist, the only two rows of the information bar.
  int info_rows = 0;
  /// Fixed blank rows between the information bar and the visualizer. Logical
  /// separation, so it is never a drawn rule.
  int info_gap_rows = 0;

  /// A little air above and below the visualizer inside the visual region, so
  /// the largest element on the screen still floats instead of touching the
  /// information bar or the Player Bar.
  int visualizer_top_pad = 0;
  int visualizer_bottom_pad = 0;

  /// The visualizer CONTAINER: the full content width, and the visual region
  /// minus its pads. Its geometry is a pure function of the terminal size, so
  /// an animating signal can never reflow it.
  int visualizer_container_rows = 0;
  int visualizer_container_columns = 0;

  /// The ACTIVE GRID inside that container: where rectangles are actually
  /// drawn. It is centered in the container both ways, and its origin is
  /// derived from the terminal size alone -- the renderer never recomputes an
  /// offset from the current spectrum, which is what keeps the object still
  /// while it animates.
  ///
  ///   container
  ///   +--------------------------------------------------+
  ///   |        <- grid_top rows of air                    |
  ///   |     +--------------------------------------+      |
  ///   |     |  grid_left cells of air, then the    |      |
  ///   |     |  rectangle grid: bands columns of    |      |
  ///   |     |  up to `levels` stacked rectangles    |      |
  ///   |     +--------------------------------------+      |
  ///   +--------------------------------------------------+
  int visualizer_grid_left = 0;
  int visualizer_grid_top = 0;
  int visualizer_grid_columns = 0;
  int visualizer_grid_rows = 0;
  /// Frequency columns that fit, and the stacked rectangles each may show.
  int visualizer_bands = 0;
  int visualizer_levels = 0;
  /// Cell stride between rectangles (block + gap), horizontal and vertical.
  int visualizer_column_stride = 0;
  int visualizer_row_stride = 0;

  /// Convenience accessors for the two numbers the renderer positions with.
  int grid_left() const { return visualizer_grid_left; }
  int grid_top() const { return visualizer_grid_top; }

  /// Blank rows between the visualizer and the Player Bar. Space, not a rule.
  int player_gap_rows = 0;
};

/// The left Sidebar: the tree on top, the playback block pinned to the bottom,
/// and the free space between them.
///
/// One source for the three numbers the renderer needs, so neither region can
/// be positioned by a blank-row count guessed from a screenshot:
///
///   +---------------- sidebar ----------------+
///   | tree_rows            (top-anchored)     |
///   |                                         |  <- flexible filler
///   | playback_top_gap    (0 or 1)            |
///   | playback_rows       (3, 2 or 0)         |
///   | playback_bottom_pad (0 or 1)            |
///   +-----------------------------------------+
struct SidebarLayout {
  /// Content height of the sidebar, inside the workspace frame.
  int rows = 0;
  /// Rows the TREE may use. The tree viewport is this, so its rows can never
  /// scroll under the playback block.
  int tree_rows = 0;
  /// Rows the now-playing block occupies. The full block is four lines
  /// (artist, title, context, NOW PLAYING); when the sidebar is short it drops
  /// its FAINTEST lines first -- the artist, then the context -- and finally
  /// disappears, so the tree is never made unusable for it.
  int playback_rows = 0;
  /// Breathing room above the block. Deliberately always 0: the flexible space
  /// above IS the separation, and the block is meant to read as one compact
  /// anchor rather than as a floating group.
  int playback_top_gap = 0;
  /// Blank rows under NOW PLAYING, before the frame. Always 0: the anchor sits
  /// on the sidebar's last row, tight against the frame.
  int playback_bottom_pad = 0;
  /// Usable text width for both regions (the frame costs one cell a side).
  int width = 0;
};

/// Every size the UI needs, derived from the live terminal size. Views must not
/// invent their own constants: all magic numbers live here so a resize is a
/// single recomputation.
struct UiMetrics {
  LayoutMode mode = LayoutMode::Large;
  int width = 0;
  int height = 0;

  // --- Which chrome is affordable ------------------------------------------
  bool show_slogan = true;
  bool show_version = true;
  bool show_clock = true;
  bool show_album_column = true;
  /// The Player Bar carries ONE playback-mode control (Shuffle). Repeat is not
  /// exposed there any more; the action itself still exists in the keymap.
  bool show_shuffle = true;
  bool show_volume = true;
  /// True while the bottom interaction box (search / prompt / confirm) is up.
  bool show_bottom_box = true;
  /// The player bar falls back to two content rows when one cannot fit.
  bool player_two_rows = false;

  // --- Vertical budget ------------------------------------------------------
  /// Workspace: one framed area plus the bottom interaction box when it
  /// is open (three rows: a framed input has a top and a bottom rule).
  int main_height = 20;
  int bottom_height = 0;
  /// Immersive: the body, then the logical gap above the control row, then the
  /// control row. The frame costs two rows of the terminal. The gap itself is
  /// `immersive.player_gap_rows`, so the body and the gap can never disagree.
  int immersive_frame_rows = 2;
  int control_height = 3;

  // --- Immersive layout -----------------------------------------------------
  /// Everything the immersive body needs, from the live terminal size. One
  /// source: the renderer reads these and invents no geometry of its own.
  ImmersiveLayout immersive;

  // --- Library workspace (Phase B) ------------------------------------------
  int workspace_tree_width = 26;
  /// The sidebar's two regions, sized here rather than in the renderer.
  SidebarLayout sidebar;
  int track_buffer_width = 80;
  bool workspace_single_pane = false;

  // --- Sidebar --------------------------------------------------------------
  /// 0 = hidden (Minimal). Icon-only when `sidebar_icon_only`.
  int sidebar_width = 16;
  bool sidebar_visible = true;
  bool sidebar_icon_only = false;

  // --- Left column / artwork ------------------------------------------------

  // --- Right column ---------------------------------------------------------
  int right_inner_width = 110;

  // --- Player bar -----------------------------------------------------------
  // The transport group is a FIXED-size cluster: the reference keeps the five
  // buttons close together and lets the progress track absorb all slack.
  int playback_button_width = 5;   // ~46 px focus box
  int playback_button_gap = 2;     // ~22 px between buttons
  int controls_width = 33;         // 5 * 5 + 4 * 2
  int progress_width = 60;         // flex-grow: recomputed from the slack
  int volume_width = 22;
  int player_padding = 2;
  /// Player-bar gap sizes. These shrink on narrow terminals BEFORE the
  /// progress bar does: whitespace is the cheapest thing to give up.
  int player_gap_small = 2;
  int player_gap_large = 3;

  // --- Track-table columns (cells) ------------------------------------------
  // One budget per collection kind, because each collection shows different
  // facts: Library adds the file size, History replaces album/duration with
  // the time the track was played, Playlist keeps title/artist/album/duration.
  // All three are computed together so a resize is still a single
  // recomputation and no renderer ever invents a column width.
  TrackColumns library_columns;
  TrackColumns history_columns;
  TrackColumns playlist_columns;
};

/// Derives every size from the current terminal size. Called on every frame, so
/// a resize is picked up without restarting or flashing.
/// `bottom_box` is true only while the workspace's bottom interaction box is
/// actually shown; when it is hidden the content gets the row back.
UiMetrics computeMetrics(int width, int height, int transport_gap = 2,
                         bool hint_line = false);

/// Human readable mode name (used by the settings page).
const char *layoutModeName(LayoutMode mode);

} // namespace termusic::ui
