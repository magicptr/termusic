#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/box.hpp>

#include "app/state.hpp"
#include "backend/mpd_backend.hpp"
#include "controller/controller.hpp"
#include "extensions/extension_registry.hpp"
#include "app/interaction.hpp"
#include "app/keymap.hpp"
#include "app/workspace_tree.hpp"
#include "ui/visualizer/renderer.hpp"
#include "ui/core/panel.hpp"
#include "ui/metrics.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"
#include "visualizer/analyzer.hpp"
#include "visualizer/beat.hpp"

namespace termusic::ui {

class Application {
 public:
public:
  Application(AppState &state, Controller &controller, MpdBackend &backend,
              VisualizerAnalyzer &analyzer,
              ThemeRegistry &themes,
              extensions::ExtensionRegistry &extensions);
  ~Application();

  int run();

  // --- Scripted mode, for the deterministic UI check (`--ui-script`) --------
  //
  // The script runs on its own thread while the MAIN thread is inside
  // `ScreenInteractive::Loop`. That is what makes it faithful: FTXUI executes a
  // posted callback inline when no application is active, so a command run
  // outside the loop would drive a button handler re-entrantly, from inside the
  // very dispatch that produced it.
  //
  // Nothing here is reachable from a normal run.
  int runScript(std::istream &script, std::ostream &out);
  void stopForScript() { stopWorkers(); }

private:
  // --- Scripted mode state (see runScript above) ----------------------------
  void runScriptLine(const std::string &line);
  std::istream *script_stream_ = nullptr;
  std::ostream *script_output_ = nullptr;
  int script_failures_ = 0;
  /// True while a script is driving: `requestQuit()` then leaves the loop exit
  /// to the runner, because the script still has assertions to report.
  bool scripting_check_ = false;
  /// The worker threads are started on demand in script mode, because a script
  /// may need to observe an asynchronous backend.
  bool backend_workers_started_ = false;
  /// Pinned layout size while a script runs; 0 means "the terminal".
  int script_width_ = 0;
  int script_height_ = 0;
  /// A one-line snapshot of everything a UI check asserts on, so a script never
  /// has to parse colours: the browsed collection, the playback context, the
  /// occurrence that is playing, and the row this frame marks.
  std::string scriptStateSummary();
  /// Feeds one key token ("j", "Enter", "Esc", "g g", "?").
  bool scriptKey(std::string_view token);
  /// The current frame as plain text, one string per row.
  std::vector<std::string> scriptFrame();
  /// Pins the layout size for the run.
  void resizeScript(int width, int height);
  /// Runs one turn of a fixed-size loop, so callbacks a command posts are
  /// deferred instead of executed inline.
  void pumpLoop();
  /// The size the layout is computed from: the terminal's, or a script's.
  std::pair<int, int> liveSize() const;
  /// The row the Track Buffer marks right now, resolved through the playback
  /// context rule (-1 for none).
  int currentPlayingRow() const;

  enum class Modal {
    None,
    NewPlaylist,
    RenamePlaylist,
    DeletePlaylist,
    ChoosePlaylist,
  };

  void buildComponents();
  void printDiagnostics() const;
  void startBackendEvents();
  void configureVisualizer();
  void startTicker();
  void requestWorkerStop();
  void stopWorkers();
  void requestQuit();
  /// Starts the shutdown deadline. Called once, before any blocking shutdown
  /// work, so the whole sequence is covered.
  void armQuitWatchdog();

  ftxui::Element renderRoot();
  ftxui::Element renderSettings();
  /// The immersive visualizer: the Spectrum, fed by the shared
  /// spectrum/beat model.
  ftxui::Element renderImmersiveVisualizer();
  /// Creates the renderer on first use. There is nothing to choose and nothing
  /// to switch, so it is built once and then only handed frames.
  void ensureVisualizerRenderer();
  /// The shared frame the renderer consumes: geometry + spectrum + beat.
  ui::VisualizerFrame visualizerFrame(const ui::VisualizerPalette &palette,
                                      double dt);
  /// The low-band onset aggregate over the last frame, for the beat detector.
  double lowBandOnset() const;
  /// Pulls the latest spectrum out of the analyzer. The UI is the single writer
  /// of `state_.visualizer`, so the data path does not depend on the event loop
  /// delivering a repaint first.
  void syncVisualizerSpectrum();
  /// Integrates the spectrum spring once per UI frame at a fixed canonical
  /// band count. Renderers only sample the result, so the mini and full
  /// visualizers can differ in width without double-integrating the motion.
  void updateVisualizerMotion();
  ftxui::Element renderTransportButton(Icon icon, bool focused,
                                       bool highlighted, int box_height,
                                       bool hovered = false,
                                       bool toggle = false);
  ftxui::Element renderPlayerBar();
  ftxui::Element squareProgress(const SliderGeometry &geometry,
                                const ftxui::Color &filled_color,
                                const ftxui::Color &empty_color) const;
  /// Phase B: the two-pane Library workspace (tree + track buffer).
  ftxui::Element renderWorkspace();
  ftxui::Element renderImmersiveNowPlaying();
  ftxui::Element renderMain();
  ftxui::Element renderTreePane();
  /// The Sidebar's lower region: the playback collection, title and artist,
  /// pinned to the bottom. Display-only.
  ftxui::Element renderSidebarPlayback();

  /// The sidebar block's line roles, TOP-DOWN, recorded while rendering.
  ///
  /// A text frame cannot show colour or weight, so the hierarchy the block is
  /// built on (faintest at the top, the anchor at the bottom) is REPORTED here
  /// and asserted by the scripted harness -- the same rule the immersive
  /// metrics follow. Empty when the block is not drawn.
  std::vector<std::string> sidebar_roles_;
  /// The accent bar's colour role while the block is drawn, empty otherwise.
  std::string sidebar_bar_;
  ftxui::Element renderTrackBuffer();
  void syncTreeFromPlaylists();
  void activateTreeNode();
  /// Clamps the tree viewport after an expand/collapse. Folding changes how
  /// many rows exist; the cursor and the open collection do not move.
  void afterTreeFold();
  /// True while a prompt or confirmation in the bottom row owns the keyboard.
  /// No pane may draw a focus band while one is up.
  bool overlayOwnsKeyboard() const;
  /// Fills the right pane from the Tree cursor WITHOUT moving the keyboard or
  /// changing the page beyond what the node requires. This is what cursor
  /// movement calls; Enter calls activateTreeNode().
  void loadTreeNode();
  /// Rebuilds the cached History view from the controller's history store.
  void refreshHistoryView();
  /// Why the active collection refuses entry edits: the exact wording the
  /// status bar shows, kept in one place so every edit path agrees.
  std::string readOnlyReason() const;
  /// The active collection's name, exactly as the Tree labels it. Single
  /// source of truth for the Track Buffer header.
  std::string collectionLabel() const;
  /// The browsed collection AS A PLAYBACK CONTEXT, so the playing marker can
  /// ask "is the list I am drawing the one that owns playback?".
  PlaybackCollection browsedPlaybackCollection() const;
  bool handleWorkspaceKey(const ftxui::Event &event);
  bool handleGlobalKey(ftxui::Event event);
  /// True when this key press resolves to Action::Quit in the active context
  /// chain. Quit is checked before the guards that swallow keys, so a focused
  /// widget can never trap the user in the application.
  bool isQuitKey(const ftxui::Event &event) const;

  // --- Round 45A: configurable keymap -------------------------------------
  void configureKeymap();
  std::vector<KeyContext> currentKeyContexts() const;
  bool performKeymapAction(Action action);
  /// Hint text for an action, read from the keymap so a rebind shows up in the
  /// status bar automatically.
  std::string keyHint(Action action) const;
  std::string keyHint(const std::vector<KeyContext> &chain, Action action) const;

  // --- Phase C: visual selection, register, paste ---------------------------
  bool visualActive() const;
  const std::vector<Song> &activeTracks() const;
  void visualRange(int &lo, int &hi) const;
  void enterVisualSelection();
  void cancelVisualSelection();
  void yankVisualSelection();
  /// Deletes History RECORDS for the inclusive row range [lo, hi]. Record ids
  /// are resolved from the displayed rows, so duplicates are addressed as the
  /// occurrences they are. Only application-owned history storage changes:
  /// no file, no Library row, no playlist entry and no queue item is touched.
  void deleteHistoryRows(int lo, int hi);
  void yankCurrentTrack();
  void pasteRegisterToTreeSelection();
  bool handlePlaylistPromptKey(const ftxui::Event &event);
  bool handleSearchPromptKey(const ftxui::Event &event);
  bool textEntryActive() const;
  bool songMatches(const Song &song, const std::string &needle) const;
  int findMatch(int from, int direction) const;
  void openSearchPrompt();
  /// The FILTER: the original row indices of `activeTracks()` whose song
  /// matches the live query, in list order. Only ever a view -- the list, the
  /// playback context and the Tree are untouched.
  void rebuildSearchRows();
  /// Moves the result cursor inside the filtered view.
  void moveSearchCursor(int delta);
  /// Enter / click on a result: leaves search, restores the COMPLETE list and
  /// puts the list cursor on the row the result came from. It never plays.
  void jumpToSearchResult(int view_index);
  /// Leaves search without selecting anything (Esc), restoring the list cursor
  /// to where it was when the box opened.
  void closeSearchPrompt();
  /// The row of `activeTracks()` a position in the filtered view stands for.
  int songIndexAt(int view_index) const;
  /// True while the box is open with a query in it (the list is filtered).
  bool searchActive() const { return search_prompt_ && !search_buffer_.empty(); }
  /// The persistent query editor: created once, never per frame.
  ftxui::Element renderSearchInput();
  bool collectionWritable() const;
  int deleteRange(int lo, int hi);
  void deleteVisualSelection();
  void deleteCurrentEntry();
  void commitPlaylistPrompt();
  void cancelPlaylistPrompt();
  bool handleWorkspaceMouse(const ftxui::Mouse &mouse);
  /// The bottom interaction box: the search field, a text prompt, or a
  /// destructive confirmation. It is the only thing the bottom row shows.
  ftxui::Element renderBottomBox();
  /// True while that box is on screen.
  bool bottomBoxVisible();
  /// A framed single-line input: the ONE input look in the application.
  ftxui::Element inputBox(const std::string &label, const std::string &text,
                          bool alert) const;
  /// A framed question with no key legend.
  ftxui::Element confirmBox(const std::string &question) const;
  /// The transient feedback overlay (bottom right, self-expiring). This is what
  /// replaced the permanent status/hint line.
  ftxui::Element renderToast();
  bool toastVisible() const;
  ftxui::Element renderModal();
  ftxui::Element renderHelpOverlay() const;

  /// Capsule slider used by the progress and volume controls.
  ftxui::Element capsuleSlider(double ratio, int width) const;

  bool handleEvent(ftxui::Event event);
  bool handleModalEvent(ftxui::Event event);
  void processTimers();
  void dispatch(Action action);
  static std::string eventToken(const ftxui::Event &event);

  void selectPage(int index);
  /// The binding table, if the panel built one. The Application talks to it
  /// instead of reaching into the section list.
  core::KeybindingsSection *keybindings();
  /// Same table, for the const predicates that only ask about its state.
  const core::KeybindingsSection *keybindings() const;
  /// The settings module the tree cursor points at, or nullptr.
  core::SettingsSection *selectedCoreSection();
  /// The `core` entry the Tree cursor currently points at.
  int coreSectionIndex() const;
  /// True only while the Tree cursor is ON the Keybindings entry AND the pane
  /// shows it. The panel's own selected index is not enough: it keeps the last
  /// entry it displayed, so after moving the cursor to a non-entry row it would
  /// still claim the table is open.
  bool keybindingsOpen() const;
  /// Feeds one key to an in-progress binding capture. Returns true when the
  /// capture consumed it.
  bool handleCaptureKey(const ftxui::Event &event);
  /// True while a new binding is being recorded: no other key may act and the
  /// global quit shortcut waits, exactly as it does for text entry.
  bool capturingBinding() const;
  /// Copies the binding table's last message into the workspace status row.
  void syncKeymapMessage();
  /// True while the open `core` entry is taking typed text.
  bool coreEditing() const;
  /// Moves the workspace's row cursor one step: the tree, the binding table or
  /// the Track Buffer, whichever the focused pane owns.
  bool moveWorkspaceCursor(int delta);
  int treePlaylistIndex() const;
  /// True when the Tree cursor is on the one protected playlist. Centralised
  /// so the rename/delete guards cannot drift apart.
  /// Loads the collection the Tree cursor now points at. Groups keep the
  /// previous content; nothing here starts playback.
  void autoLoadTreeCursor();
  void openModal(Modal modal);
  void closeModal();
  void confirmModal();
  void focusCurrentList();

  std::string songRow(const Song &song, int ordinal, int width,
                      bool playing) const;
  /// The row WITHOUT the leading playing marker, so the marker can be drawn as
  /// its own element and keep its own colour on a highlighted cursor row.
  std::string songRowBody(const Song &song, int ordinal) const;
  /// The same row split into its columns, so the Track Buffer can give the
  /// title, artist, album, duration, size and play time their own semantic
  /// colours. Concatenating the parts reproduces songRowBody() byte for byte,
  /// so the column geometry cannot drift.
  ///
  /// Every column the budget carries is emitted for EVERY row -- an absent
  /// value is blank padding, never a missing cell -- because a row that
  /// dropped a column would shift every column after it and break the table
  /// alignment with its own header.
  struct SongRowParts {
    std::string ordinal;
    std::string icon;
    std::string title_lead;
    std::string title;
    std::string artist;
    std::string album;
    std::string duration;
    std::string size;
    std::string played;
  };
  /// `columns` is the active collection's budget; each part is padded to its
  /// own column width, independent of the others.
  SongRowParts songRowParts(const Song &song, int ordinal,
                            const TrackColumns &columns) const;
  /// The column budget of the collection currently in the Track Buffer.
  const TrackColumns &activeColumns() const;
  /// Human name of the active collection kind, used in tests and messages.
  std::string_view activeCollectionName() const;
  ftxui::Element emptyState(std::string title, std::string detail) const;
  ftxui::Element panel(ftxui::Element content, std::string title = {}) const;

  AppState &state_;
  Controller &controller_;
  MpdBackend &backend_;
  VisualizerAnalyzer &analyzer_;
  /// First value seen for each key asserted with `expect-state-same`, so a
  /// script can pin a "this must not change" rule to the value it started with.
  std::map<std::string, std::string> script_baseline_;
  /// THE visualizer: the Spectrum, and the only renderer there is. The
  /// Application never branches on it -- it hands the renderer the shared frame
  /// (spectrum, beat, grid geometry, palette) and draws what comes back.
  std::unique_ptr<ui::VisualizerRenderer> visualizer_;
  /// The beat envelope. Persistence for the animation, never geometry.
  BeatState beat_;
  /// The low-band energy of the last rendered frame, reported by the script
  /// state so the onset test can be inspected.
  double low_energy_ = 0.0;
  /// When the previous frame integrated the visualizer motion, so the attack
  /// and decay rates are time-based rather than frame-count-based.
  std::chrono::steady_clock::time_point last_visualizer_time_{};
  ThemeRegistry &themes_;
  extensions::ExtensionRegistry &extensions_;
  Theme theme_;

  ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();
  ftxui::Component root_;
  ftxui::Component main_container_;
  ftxui::Component page_tab_;
  ftxui::Component library_section_component_;
  /// Every entry under `core`, assembled. The Application renders it and
  /// installs its component; which entries exist is the panel's business.
  std::unique_ptr<core::CorePanel> core_panel_;
  /// The `core` module tree's capability table. `CoreContext` stores
  /// REFERENCES to these std::function members on purpose: binding a reference
  /// to a temporary would leave every section calling an empty function.
  std::function<int()> core_start_page_index_;
  std::function<void(int)> core_set_start_page_index_;
  std::function<bool()> core_save_config_;
  std::function<void()> core_theme_changed_;
  std::function<void()> core_visualizer_changed_;
  std::function<void()> core_update_database_;
  /// Where the configuration file lives, for the Help module.
  std::function<std::string()> core_config_path_;
  std::function<bool()> core_content_focused_;
  std::function<const std::string &()> core_transient_message_;
  std::function<bool(KeyContext, Action, const std::vector<std::string> &,
                     std::string *)>
      core_preview_binding_;
  std::function<bool(KeyContext, Action, const std::vector<std::string> &)>
      core_save_binding_;
  std::function<void()> core_reset_bindings_;
  std::function<void(std::string, int, std::string)> core_connection_changed_;
  std::function<ftxui::ButtonOption()> core_compact_button_;
  std::unique_ptr<core::CoreContext> core_context_;
  ftxui::Component modal_input_;
  ftxui::Component playlist_target_menu_;
  ftxui::Component player_container_;

  ftxui::Component shuffle_button_;
  ftxui::Component previous_button_;
  ftxui::Component play_button_;
  ftxui::Component next_button_;
  ftxui::Component progress_slider_component_;
  ftxui::Component volume_slider_component_;


  std::vector<std::string> playlist_target_rows_;
  int page_index_ = 1;
  /// Deleting a saved playlist is destructive, so it waits for confirmation.
  bool delete_playlist_pending_ = false;
  /// The name prompt is shared: `a` creates, `r` renames.
  bool playlist_prompt_rename_ = false;
  std::string rename_original_;
  int playlist_target_ = 0;
  int volume_slider_ = 0;
  int volume_min_ = 0;
  int volume_max_ = 100;
  int volume_increment_ = 1;
  double progress_slider_ = 0.0;
  double progress_min_ = 0.0;
  double progress_max_ = 1.0;
  double progress_increment_ = 1.0;
  bool updating_sliders_ = false;

  // Recomputed before every render. Terminal cells cannot be physically
  // scaled, so the layout and image sampling scale together instead.

  // --- Responsive geometry, recomputed from the terminal size every frame ---
  UiMetrics metrics_;
  IconSet icon_set_ = IconSet::NerdFont;
  SliderRenderer slider_renderer_ = SliderRenderer::Braille;
  bool ui_slider_test_ = false;
  std::chrono::steady_clock::time_point ui_test_start_{};
  bool help_visible_ = false;
  std::string clock_text_;
  std::time_t last_clock_stamp_ = 0;

  // --- Pointer state, refreshed from mouse events ---------------------------
  ftxui::Box progress_box_;
  ftxui::Box volume_box_;
  // One hit box per transport control, filled by reflect() during render so
  // hit-testing uses the real laid-out geometry.
  ftxui::Box shuffle_box_;
  ftxui::Box previous_box_;
  ftxui::Box play_box_;
  ftxui::Box next_box_;
  // One hit box per sidebar row, filled by reflect() during render.
  /// Resolved transport control under the pointer / held down, -1 for none.
  int hover_control_ = -1;
  int pressed_control_ = -1;
  bool mouse_debug_ = false;
  bool motion_test_ = false;
  // --- Phase B workspace state ---------------------------------------------
  WorkspaceTree workspace_tree_;
  VimMode vim_mode_ = VimMode::Normal;
  /// Startup focus is the Tree, so the very first `l` (the natural "move
  /// right" a user tries) does something instead of hitting the Track list's
  /// designed right-edge no-op.
  WorkspacePane workspace_pane_ = WorkspacePane::Tree;
  int tree_scroll_ = 0;
  int track_cursor_ = 0;
  int track_scroll_ = 0;
  std::vector<ftxui::Box> tree_boxes_;
  std::vector<ftxui::Box> track_row_boxes_;
  std::vector<int> track_row_index_;
  ftxui::Box track_area_box_;
  std::string tree_synced_signature_;
  /// Which collection the Track Buffer is showing. The MPD play queue is NOT
  /// one of them: it is an internal playback mechanism, never a browsable
  /// collection. Exactly one is active at a time, which is what makes the
  /// header, the writability rules and the hints agree.
  enum class ActiveCollection { Library, History, Playlist };
  ActiveCollection active_collection_ = ActiveCollection::Playlist;
  /// The playback-history view, newest first. Kept out of
  /// `state_.library.songs` so a database refresh can never clobber it.
  std::vector<Song> history_songs_;
  std::size_t history_revision_seen_ = static_cast<std::size_t>(-1);
  /// Latches a leading `g` for the section-level `g n` / `g l` chords.
  Keymap keymap_;
  std::vector<std::string> keymap_warnings_;
  /// Visual selection is one contiguous range: a fixed anchor plus the moving
  /// track cursor. -1 means no active selection.
  int visual_anchor_ = -1;
  /// Buffer size when the anchor was taken, so an asynchronous refresh can
  /// cancel the selection instead of letting the range address stale rows.
  std::size_t visual_list_size_ = 0;
  /// Transient one-line feedback ("3 tracks yanked"), never modal. It is
  /// rendered as the self-expiring toast, never as a permanent status row.
  std::string visual_message_;
  /// The message the toast is currently showing, and when it appeared. The
  /// pair is what makes the feedback transient: processTimers() stamps a change
  /// and clears the message once kToastMs has passed.
  std::string toast_message_;
  std::chrono::steady_clock::time_point toast_at_{};
  /// True while visual_message_ reports a REFUSED action, so the status bar
  /// can give it the restrained Red accent without colouring the whole bar.
  bool visual_message_error_ = false;
  /// Inline "NEW PLAYLIST:" prompt opened by `a`. Not a modal: the status bar
  /// becomes the input line.
  bool playlist_prompt_ = false;
  std::string playlist_prompt_text_;

  // --- Buffer search: the RIGHT list only, filtered -------------------------
  /// `/` input. The box filters the right pane's list and locates a row in it:
  /// it never touches the Tree, the collections or the playback context.
  bool search_prompt_ = false;
  std::string search_buffer_;
  /// The accepted pattern that `n` / `N` walk after the box closes.
  std::string search_pattern_;
  /// Where the list cursor was when the box opened -- Esc puts it back there.
  int search_anchor_ = 0;
  /// True when the live query matches nothing: the box turns Red and the pane
  /// says so instead of showing the list.
  bool search_no_match_ = false;
  /// The filtered VIEW: original row indices into `activeTracks()`. Empty when
  /// no query is active; never a copy of the songs themselves, so a result
  /// knows exactly which occurrence it stands for.
  std::vector<int> search_rows_;
  /// The result cursor and its viewport, in view positions. Separate from
  /// `track_cursor_` / `track_scroll_`, which keep the real list position.
  int search_cursor_ = 0;
  int search_scroll_ = 0;
  /// The query editor. FTXUI's Input, created ONCE: it owns the caret, the
  /// UTF-8 editing and the terminal cursor the IME anchors to.
  ftxui::Component search_input_;
  /// Which collection the Track Buffer currently shows, for write decisions:
  /// the open saved playlist's own name (identified by node id), or nothing for
  /// the media database and History.
  std::string active_playlist_name_;
  int active_playlist_index_ = -1;
  const termusic::TreeNode *currentTreeNode() const;
  bool flux_enabled_ = true;
  /// Frame-level headroom gain. Falls fast on loud passages, recovers slowly.
  float headroom_gain_ = 1.0F;
  std::chrono::steady_clock::time_point last_motion_time_{};
  // Motion metrics accumulated by --ui-visualizer-motion-test.
  int motion_frames_ = 0;
  double motion_sum_delta_ = 0.0;
  double motion_max_delta_ = 0.0;
  int motion_moving_bands_ = 0;
  int motion_direction_changes_ = 0;
  std::vector<float> motion_previous_;
  std::vector<float> motion_last_direction_;
  std::string mouse_debug_text_;
  /// 0 = none, 1 = progress drag, 2 = volume drag.
  int drag_target_ = 0;
  /// Live drag previews in 0..1; negative means "not dragging". The UI follows
  /// these while dragging and only commits one MPD call on release.
  double drag_progress_ = -1.0;
  double drag_volume_ = -1.0;

  std::string play_label_ = "▶";
  std::string shuffle_label_ = "⇄";
  std::string modal_text_;

  Modal modal_ = Modal::None;
  std::chrono::steady_clock::time_point last_click_{};
  int last_click_track_ = -1;
  /// When the last connection attempt started. Initialized on the first
  /// attempt, so startup does not fire a second one immediately afterwards.
  std::chrono::steady_clock::time_point last_reconnect_{};
  /// Failed automatic attempts since the last successful connection. Indexes
  /// the backoff ladder; there is no limit, so a server that comes back hours
  /// later is still picked up without the user touching anything.
  int reconnect_attempts_ = 0;
  /// The endpoint generation the ladder belongs to. When it changes (a manual
  /// reconnect, a new host/port), the ladder restarts.
  std::size_t connection_epoch_seen_ = 0;

  std::mutex ticker_mutex_;
  std::condition_variable_any ticker_wakeup_;
  std::atomic<bool> quitting_{false};
  std::jthread ticker_thread_;
  std::atomic<bool> ticker_fast_{false};
  /// True while the SEARCH LINE owns the keyboard. The analyzer thread reads it
  /// to stop requesting a repaint per analysed frame: the spectrum is not on
  /// screen then, and a repaint storm under an active IME is exactly what makes
  /// the composition and the candidate window flicker.
  std::atomic<bool> search_typing_{false};
};

} // namespace termusic::ui
