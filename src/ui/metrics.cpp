#include "ui/metrics.hpp"

#include <algorithm>
#include <string>

namespace termusic::ui {
namespace {


/// The control row is a bare line, not a panel: one row normally, two when
/// the transport has to stack above the timeline.
constexpr int kPlayerRowsFull = 1;
constexpr int kPlayerRowsTwo = 2;

} // namespace

const char *layoutModeName(LayoutMode mode) {
  switch (mode) {
  case LayoutMode::Large:
    return "Large";
  case LayoutMode::Medium:
    return "Medium";
  case LayoutMode::Compact:
    return "Compact";
  case LayoutMode::Minimal:
    return "Minimal";
  }
  return "Large";
}

UiMetrics computeMetrics(int width, int height, int transport_gap,
                         bool bottom_box) {
  UiMetrics m;
  m.width = std::max(1, width);
  m.height = std::max(1, height);

  // --- Bucket selection -----------------------------------------------------
  if (m.width >= 120 && m.height >= 36) {
    m.mode = LayoutMode::Large;
  } else if (m.width >= 90 && m.height >= 28) {
    m.mode = LayoutMode::Medium;
  } else if (m.width >= 65 && m.height >= 22) {
    m.mode = LayoutMode::Compact;
  } else {
    m.mode = LayoutMode::Minimal;
  }

  const bool large = m.mode == LayoutMode::Large;
  const bool medium = m.mode == LayoutMode::Medium;
  const bool compact = m.mode == LayoutMode::Compact;
  const bool minimal = m.mode == LayoutMode::Minimal;

  // --- Chrome that costs columns -------------------------------------------
  m.show_slogan = large || medium;
  m.show_version = large;
  m.show_clock = !minimal;
  m.show_shuffle = !minimal;
  m.show_volume = !minimal && m.width >= 72;
  m.show_album_column = large || medium;

  // Sidebar: full text on Large, narrower on Medium, icon-only on Compact and
  // hidden entirely on Minimal so the page content never disappears.
  if (large) {
    m.sidebar_width = 16;
    m.sidebar_visible = true;
  } else if (medium) {
    m.sidebar_width = 13;
    m.sidebar_visible = true;
  } else if (compact) {
    m.sidebar_width = 4;
    m.sidebar_visible = true;
    m.sidebar_icon_only = true;
  } else {
    m.sidebar_width = 0;
    m.sidebar_visible = false;
  }

  // --- Vertical budget ------------------------------------------------------
  // Two modes, two budgets. The workspace spends its height on ONE framed
  // area plus, while it is open, the bottom interaction box; the immersive
  // mode spends it on the song plus a single control row. Nothing else is
  // charged -- there is no permanent status row any more.
  m.show_bottom_box = bottom_box && m.height >= 16;
  m.bottom_height = m.show_bottom_box ? 3 : 0;
  m.control_height = m.player_two_rows ? kPlayerRowsTwo : kPlayerRowsFull;
  m.main_height = std::max(3, m.height - m.bottom_height);
  // --- Player bar widths ----------------------------------------------------
  // Fixed-size cluster + fixed labels; the progress track flexes into whatever
  // remains. This replaces the old percentage split, which spread the five
  // buttons across the whole left third.
  m.playback_button_width = (m.width >= 70) ? 5 : 4;
  m.playback_button_gap =
      std::clamp(transport_gap, 0, 4);
  // ONE playback-mode control (Shuffle), then previous, play and next. The
  // cells Repeat used to occupy are not left blank: they are simply not
  // reserved, so the progress track below grows into them.
  m.controls_width = 4 * m.playback_button_width + 3 * m.playback_button_gap;
  m.player_padding = (m.width >= 100) ? 2 : 1;
  const bool tight_player = m.width < 132;
  m.player_gap_small = tight_player ? 1 : 2;
  m.player_gap_large = tight_player ? 1 : 3;
  if (minimal) {
    // Only previous / play / next survive.
    m.controls_width = 3 * m.playback_button_width + 2 * m.playback_button_gap;
    m.volume_width = 0;
  } else if (compact) {
    m.volume_width = std::clamp(m.width * 10 / 100, 6, 14);
  } else if (medium) {
    m.volume_width = std::clamp(m.width * 11 / 100, 8, 17);
  } else {
    m.volume_width = std::clamp(m.width * 12 / 100, 10, 21);
  }

  // Fixed costs: padding, gaps, both time labels, divider and the volume group.
  // The gaps are derived from the responsive sizes so that widening a gap and
  // reserving for it can never disagree.
  const int kGaps = 4 * m.player_gap_small + 2 * m.player_gap_large + 4;
  constexpr int kTimes = 5 + 5;
  const int volume_group = m.show_volume ? 1 + 4 + m.volume_width : 0;
  const int reserved =
      m.controls_width + kGaps + kTimes + volume_group + 2 * m.player_padding;
  // Everything on the timeline row except the progress and volume bars.
  const int timeline_fixed =
      kTimes + 4 * m.player_gap_small + 2 * m.player_gap_large + 2 +
      2 * m.player_padding +          // both ends
      (m.show_volume ? 6 : 0);        // divider + speaker + "  75%"
  m.progress_width = m.width - reserved;
  if (m.progress_width < 18) {
    // One row cannot hold everything: stack the transport above the timeline.
    m.player_two_rows = true;
    m.control_height = kPlayerRowsTwo;
    // Must use the same accounting as the single-row case, otherwise the
    // stacked timeline overflows by a few cells and the border is lost.
    m.progress_width = std::max(
        10, m.width - timeline_fixed - (m.show_volume ? m.volume_width : 0) - 2);
  }
  // The scrub bar keeps a usable floor: if only crumbs are left, give the
  // volume group up rather than the timeline.
  if (m.progress_width < 24 && m.show_volume && !m.player_two_rows) {
    const int deficit = 24 - m.progress_width;
    const int reclaimed = std::min(deficit, std::max(0, m.volume_width - 6));
    m.volume_width -= reclaimed;
    m.progress_width += reclaimed;
  }
  m.player_two_rows = m.player_two_rows || m.progress_width < 18;
  m.progress_width = std::max(8, m.progress_width);

  // --- Library workspace ----------------------------------------------------
  // Both panes need a useful minimum; below that only the focused one shows,
  // which keeps the same Tree + Track Buffer mental model.
  m.workspace_single_pane = m.width < 76;
  m.workspace_tree_width = std::clamp(m.width * 26 / 100, 18, 34);
  m.track_buffer_width =
      m.workspace_single_pane ? m.width
                              : std::max(20, m.width - m.workspace_tree_width - 3);

  // --- Sidebar: tree on top, now-playing block at the bottom ----------------
  // The block is four semantic rows, built from the bottom up: NOW PLAYING is
  // the anchor, then the playback context, then the title, then the artist.
  // When the sidebar is too short it gives up its FAINTEST line first (the
  // artist, then the context), and only then disappears. There is no gap above
  // it and no padding below it: the flexible space above the block is the only
  // separation, and NOW PLAYING sits on the sidebar's last row.
  //
  // This is computed AFTER the workspace widths above on purpose: the block's
  // text budget is the sidebar column's own width, so a line can never be
  // handed a box the column does not have. In single-pane mode the sidebar IS
  // the pane, so the budget is the whole pane.
  {
    SidebarLayout &side = m.sidebar;
    side.rows = std::max(0, m.main_height - m.immersive_frame_rows);
    side.width = std::max(
        4, (m.workspace_single_pane ? m.width : m.workspace_tree_width) - 2);
    side.playback_rows = side.rows >= 16 ? 4 : (side.rows >= 12 ? 3
                                                : (side.rows >= 9 ? 2 : 0));
    side.playback_top_gap = 0;
    side.playback_bottom_pad = 0;
    side.tree_rows = std::max(
        1, side.rows - side.playback_rows - side.playback_top_gap -
               side.playback_bottom_pad);
  }

  // --- Track-table columns --------------------------------------------------
  // One row is built as: "▶ " + index + " " + title + " " + artist + " " +
  // album + " " + duration/size/played, so every separator is budgeted
  // explicitly and a row can never be wider than the panel it is drawn in.
  //
  // Each variant budgets its FIXED columns first and splits the remainder
  // between the text columns by exact remainder. That is what keeps a row
  // inside the panel: independent per-column floors could sum to more than the
  // budget and push the border off the screen edge.
  //
  // Marker and lead: "▶ " is two cells, then the space that separates the
  // marker from the first real column.
  constexpr int kRowLead = 2 + 1;
  /// The music icon in front of every track title, plus its separating space.
  /// It is a column like any other: reserved once here, subtracted from the
  /// title budget, so a row is never wider than the panel it is drawn in.
  constexpr int kTrackIcon = 2;
  // The columns live in the TRACK BUFFER pane, so they are budgeted against
  // that pane's own width -- not the terminal's. Budgeting against the terminal
  // laid the last columns out past the pane, where FTXUI clipped them and the
  // table silently lost its Duration and Size.
  // Inside the pane: one border, then a one-cell inset for the row cursor.
  const int table = std::max(12, m.track_buffer_width - 2) - 2;

  // A row number is a convenience, never the reason a column exists.
  const int index_width =
      table >= 40 ? std::clamp(table * 4 / 100, 0, table / 6) : 0;
  const int index_gap = index_width > 0 ? 1 : 0;

  // Column widths are chosen from the widest string each one must hold, so a
  // value is only ever ellipsized when the terminal itself is too narrow.
  // Album takes whatever is left over, which is what makes the split exact.
  const auto split = [](int budget, int title_percent, int artist_percent,
                        bool with_album) {
    TrackColumns columns;
    columns.title_lead = 1;
    columns.title = budget * title_percent / 100;
    columns.artist = budget * artist_percent / 100;
    columns.album = with_album ? budget - columns.title - columns.artist : 0;
    return columns;
  };

  // --- Immersive layout -----------------------------------------------------
  // One centered column, sized here and nowhere else:
  //
  //   information bar (title, artist)   centered, fixed at the top
  //   info_gap_rows                     blank space, never a drawn rule
  //   visualizer (the primary content)  full content width, most of the height
  //   player_gap_rows                   blank space, never a drawn rule
  //   Player Bar                        its own height, not this layout's
  //
  // The visualizer is the ONLY primary content, so it takes the released
  // columns and rows rather than a column beside something else. The margins,
  // gaps and pads are the breathing room that keeps it from touching a border.
  {
    ImmersiveLayout &layout = m.immersive;

    // Both logical gaps are space, and both shrink before the visualizer does:
    // at a comfortable height they are two rows each, on a short terminal one.
    const bool roomy = m.height >= 34;
    const bool comfortable = m.height >= 30;
    layout.info_gap_rows = roomy ? 2 : 1;
    layout.player_gap_rows = comfortable ? 2 : 1;

    layout.body_height = std::max(
        4, m.height - m.immersive_frame_rows - layout.player_gap_rows -
               m.control_height);
    layout.body_width = std::max(8, m.width - 4);

    // Horizontal breathing room: symmetric now that nothing sits beside the
    // visualizer. It is a share of the terminal and shrinks with the width.
    const int pad = layout.body_width >= 118   ? 4
                    : layout.body_width >= 96  ? 3
                    : layout.body_width >= 76  ? 2
                                               : 1;
    // Never let a margin eat the content: at least 20 cells stay usable.
    layout.outer_left_pad =
        std::max(1, std::min(pad, (layout.body_width - 20) / 2));
    layout.outer_right_pad = layout.outer_left_pad;
    // The frame's inside is two cells wider than `body_width` (which charges one
    // cell of air at each end). Sizing the content from the INSIDE is what makes
    // the margins land symmetrically: margin + content + margin fills it
    // exactly, so the centered information rows are centered to the cell.
    layout.content_width =
        std::max(8, layout.body_width + 2 - layout.outer_left_pad -
                        layout.outer_right_pad);

    layout.info_rows = 2;
    const int visual_area = std::max(
        4, layout.body_height - layout.info_rows - layout.info_gap_rows);
    // One row of air above and below while there is room for it; the pads are
    // given up before the spectrum loses rows.
    layout.visualizer_top_pad = visual_area >= 8 ? 1 : 0;
    layout.visualizer_bottom_pad = visual_area >= 12 ? 1 : 0;
    ImmersiveLayout &l = layout;
    l.visualizer_container_rows =
        std::max(4, visual_area - l.visualizer_top_pad -
                        l.visualizer_bottom_pad);
    l.visualizer_container_columns = l.content_width;

    // The ACTIVE GRID floats inside that container rather than filling it: the
    // composition keeps a real margin on every side, so the rectangles read as
    // one centered object instead of wallpaper. Every number below is a pure
    // function of the terminal size.
    l.visualizer_column_stride = 2; // one rectangle cell + one cell of air
    // Two-row vertical stride keeps the rectangles separate; on a short
    // terminal the gap is given up before the number of levels is.
    l.visualizer_row_stride = l.visualizer_container_rows >= 12 ? 2 : 1;
    const int edge_x = std::clamp(l.visualizer_container_columns / 12, 2, 12);
    const int edge_y = std::clamp(l.visualizer_container_rows / 12, 0, 3);
    const int usable_w =
        std::max(8, l.visualizer_container_columns - 2 * edge_x);
    const int usable_h =
        std::max(3, l.visualizer_container_rows - 2 * edge_y);
    l.visualizer_bands =
        std::max(1, usable_w / l.visualizer_column_stride);
    l.visualizer_levels = std::max(1, usable_h / l.visualizer_row_stride);
    // The drawn grid: `count` rectangles and the `count - 1` gaps between them,
    // so the last rectangle can never be pushed outside the box by rounding --
    // and a stride of one (no gaps) is one cell per level, not one less.
    l.visualizer_grid_columns =
        l.visualizer_bands * l.visualizer_column_stride -
        (l.visualizer_column_stride - 1);
    l.visualizer_grid_rows = l.visualizer_levels * l.visualizer_row_stride -
                             (l.visualizer_row_stride - 1);
    // Centered: the two inactive margins differ by at most one cell.
    l.visualizer_grid_left = (l.visualizer_container_columns -
                              l.visualizer_grid_columns) / 2;
    l.visualizer_grid_top =
        (l.visualizer_container_rows - l.visualizer_grid_rows) / 2;
  }

  // Every section owns the full main region: there is no presentation column
  // to subtract. Immersive now-playing draws its own layout and reads only
  // `main_height` / `width`.
  m.right_inner_width = std::max(8, m.width - 2);

  if (minimal) {
    // One line, one idea: what is playing. Only title and duration survive.
    const int minimal_index = table >= 34 ? index_width : 0;
    const int minimal_index_gap = minimal_index > 0 ? 1 : 0;
    constexpr int kMinimalDuration = 6;
    m.library_columns =
        split(std::max(0, table - kRowLead - minimal_index - minimal_index_gap -
                                   1 - kMinimalDuration - kTrackIcon),
              100, 0, false);
    m.library_columns.index = minimal_index;
    m.library_columns.icon = kTrackIcon;
    m.library_columns.title_lead = 1;
    m.library_columns.duration = kMinimalDuration;
    // History and Playlist keep the same single text column and duration. The
    // 19-cell timestamp and the album column genuinely do not fit here, and a
    // column of ellipses would be worse than the two facts that do.
    m.history_columns = m.library_columns;
    m.playlist_columns = m.library_columns;
    return m;
  }

  const bool compact_mode = m.mode == LayoutMode::Compact;

  // --- Library: index, title, artist, album, duration, size -----------------
  // "1023.9 MB" is the longest string formatFileSize() produces. Keeping the
  // full nine cells would starve the text columns on a compact terminal, so
  // the size column shrinks and then disappears before they do.
  const int size_width = table >= 62 ? 9 : (table >= 52 ? 6 : 0);
  const int size_gap = size_width > 0 ? 1 : 0;
  const int library_duration =
      compact_mode ? std::clamp(table * 12 / 100, 4, 7)
                   : std::clamp(table * 8 / 100, 4, 9);
  // The music icon is charged to the TITLE column's budget (it sits directly in
  // front of it), which is the column that already absorbs ellipsis -- so the
  // artist and album keep the widths they had before icons existed.
  const int library_text = std::max(
      0, table - kRowLead - index_width - index_gap - size_gap - size_width -
             1 - library_duration - kTrackIcon);
  m.library_columns = compact_mode ? split(library_text, 72, 28, false)
                                   : split(library_text, 50, 22, true);
  m.library_columns.index = index_width;
  m.library_columns.icon = kTrackIcon;
  m.library_columns.title_lead = 1;
  m.library_columns.duration = library_duration;
  m.library_columns.size = size_width;

  // --- History: index, title, artist, played-at -----------------------------
  // No album and no duration: the question a history row answers is "what, and
  // when". "2026-02-14 09:31:05" is 19 columns of real information that cannot
  // be shortened without losing the seconds, so it is budgeted FIRST -- before
  // the row number and even before the artist -- and nothing else is allowed to
  // take its room. A history row without its time is not a history row.
  const int played_width = 19;
  const int played_gap = 1;
  // The row number is the first luxury to go, then the artist shrinks, and
  // below that the timestamp itself is dropped: there is no width at which it
  // could still be shown.
  const int history_index = table >= 60 ? index_width : 0;
  const int history_index_gap = history_index > 0 ? 1 : 0;
  const bool history_plays =
      table - kRowLead - history_index - history_index_gap - played_gap >=
      played_width + 2;
  const int history_text =
      history_plays
          ? std::max(0, table - kRowLead - history_index - history_index_gap -
                             played_gap - played_width - kTrackIcon)
          : std::max(0, table - kRowLead - history_index - history_index_gap -
                             kTrackIcon);
  m.history_columns = split(history_text, history_plays ? 58 : 72,
                            history_plays ? 42 : 28, false);
  m.history_columns.index = history_index;
  m.history_columns.icon = kTrackIcon;
  m.history_columns.title_lead = 1;
  m.history_columns.played = history_plays ? played_width : 0;
  // Explicit: History answers "what, and when". Dropping the duration and the
  // size is exactly what pays for the timestamp column.
  m.history_columns.duration = 0;
  m.history_columns.size = 0;

  // --- Playlist: index, title, artist, album, duration ----------------------
  // `listplaylistinfo` carries no `size` attribute, so there is no size column
  // to budget: a saved playlist's rows show exactly what MPD can report.
  const int playlist_duration =
      compact_mode ? std::clamp(table * 12 / 100, 4, 7)
                   : std::clamp(table * 8 / 100, 4, 9);
  const int playlist_text =
      std::max(0, table - kRowLead - index_width - index_gap - 1 -
                      playlist_duration - kTrackIcon);
  m.playlist_columns = compact_mode ? split(playlist_text, 72, 28, false)
                                    : split(playlist_text, 50, 22, true);
  m.playlist_columns.index = index_width;
  m.playlist_columns.icon = kTrackIcon;
  m.playlist_columns.title_lead = 1;
  m.playlist_columns.duration = playlist_duration;

  return m;
}

} // namespace termusic::ui
