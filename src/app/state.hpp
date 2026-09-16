#pragma once

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/actions.hpp"
#include "app/interaction.hpp"
#include "app/register.hpp"

namespace termusic {

/// The protected playlist shown at the top of PlayLists. It is a virtual view
/// of the MPD media database, not an MPD stored playlist.
inline constexpr std::string_view kDefaultPlaylistName = "Default";

inline constexpr bool isDefaultPlaylistName(std::string_view name) {
  return name == kDefaultPlaylistName;
}

/// Prepends virtual Default and removes a colliding stored name.
std::vector<std::string>
playlistsWithDefault(std::vector<std::string> saved_playlists);

/// Top-level sections, in navigation order. There is deliberately NO
/// `NowPlaying` page: the immersive song display is a *presentation mode*
/// (`PresentationMode`) that overlays whichever section is active, not a
/// section of its own. Its legacy spelling survives only as a config token.
/// `Playlist` supersedes the old `Queue` page; the legacy config token is still
/// accepted on load.
enum class Page {
  Library,
  Settings,
};

/// The single primary visual in Immersive mode. A value (rather than two
/// booleans) makes Spectrum and Disc mutually exclusive by construction.
enum class DisplayMode {
  Spectrum,
  Disc,
};

/// Number of entries in the sidebar.
inline constexpr int kPageCount = 2;

/// What a `core` entry IS, semantically. The kind is the whole classification
/// the tree needs: an entry that only SHOWS something is information, an entry
/// that can be changed is a settings module. The renderer maps the kind to an
/// icon, so adding a section never means editing tree rendering.
enum class CoreSectionKind {
  Information, ///< read-only text: a document
  Settings,    ///< configurable module: a gear
};

/// One entry of `core`.
///
/// `core` is the second top-level directory and its entries behave exactly
/// like the ones under `vault`: selectable rows that load their content into
/// the right pane. They are NOT file names -- the user operates settings, not
/// text files. `section` is the index into the settings sections, so the tree
/// and the editor can never disagree about which entry edits what.
struct ConfigFileEntry {
  std::string_view id;    // stable node id, e.g. "core:appearance"
  std::string_view label; // name shown in the tree
  int section;            // settings section index
  std::string_view summary;
  CoreSectionKind kind = CoreSectionKind::Settings;
};

/// The order here IS the order of the tree AND the order of the panes:
/// frequently used configuration first (General, Appearance, Keybindings),
/// then extension management (Plugins), then the two read-only pages
/// (About, Help). The MPD server settings are part of General -- there is no
/// separate Connection entry.
inline constexpr std::array<ConfigFileEntry, 6> kCoreSections = {{
    {"core:general", "General", 0, "startup, steps and the MPD server",
     CoreSectionKind::Settings},
    {"core:appearance", "Appearance", 1, "theme and visualizer",
     CoreSectionKind::Settings},
    {"core:keybindings", "Keybindings", 2, "key bindings",
     CoreSectionKind::Settings},
    {"core:plugins", "Plugins", 3, "extensions", CoreSectionKind::Settings},
    {"core:about", "About", 4, "version and credits",
     CoreSectionKind::Information},
    {"core:help", "Help", 5, "how to use termusic",
     CoreSectionKind::Information},
}};

/// Navigation label and the config token used to persist a page.
struct Song;

/// Canonical conversion: the URI is the identity, metadata is display only.
TrackRef trackRefFromSong(const Song &song);

std::string_view pageLabel(Page page);
/// Abbreviated label for a narrow top bar.
std::string_view pageShortLabel(Page page);
std::string_view pageToken(Page page);
std::optional<Page> pageFromToken(std::string_view token);
/// Fixed sidebar order; the only place the order is defined.
const std::vector<Page> &allPages();

enum class PlaybackState {
  Stopped,
  Playing,
  Paused,
};

/// Which panel currently owns the keyboard. The workspace's two panes (Tree
/// and Track List) are tracked separately by the UI, because the Controller
/// never needs to tell them apart: both act on the selected collection.
enum class FocusArea {
  Library,
  Settings,
  PlayerBar,
};

/// One track, always built from MPD metadata (requirements v2 §16).
struct Song {
  std::string uri;
  /// Empty for MPD/local-library songs. Streaming songs retain their stable
  /// provider identity separately because `uri` may be a short-lived signed
  /// playback URL.
  std::string source_id;
  std::string source_track_id;
  std::string title;
  std::string artist;
  std::string album;
  /// Extra metadata the immersive view shows when MPD supplies it. Empty
  /// means "unknown": the row is hidden rather than filled with a guess.
  std::string year;
  std::string genre;
  std::string format;
  double duration_seconds = 0.0;
  /// Live streams have no meaningful fixed duration or seek endpoint.
  bool is_live_stream = false;
  /// Size of the underlying file in bytes, straight from MPD's `size`
  /// attribute. 0 means "MPD did not report one" -- the Library table then
  /// shows "--" instead of inventing a number. Only the media database and a
  /// directory listing carry it: `listplaylistinfo` has no `size` attribute,
  /// so a saved-playlist row legitimately has none.
  double file_size_bytes = 0.0;
  /// When this track was played, as Unix epoch seconds. Filled by the History
  /// view only; 0 means "no timestamp recorded".
  long long played_at_epoch = 0;
  /// Identity of the History RECORD this row came from, or 0 for every other
  /// collection. It is an occurrence id, not a URI: two History rows may share
  /// a URI and are still distinct, deletable records.
  long long history_record_id = 0;
  /// Which duplicate of its own URI this row is, counted from 1 within the
  /// collection it belongs to. `A B A C` gives the second A the value 2, which
  /// is what lets "the second A is playing" be expressed at all -- the URI
  /// cannot say it. 0 means "not filled in", which is treated as 1.
  int playback_occurrence = 0;
  std::optional<unsigned> queue_id;
  std::optional<unsigned> queue_position;

  /// Title with the documented fallback chain (§6.2).
  std::string displayTitle() const;
  std::string displayArtist() const;
  std::string displayAlbum() const;
};

/// Which collection owns the current playback.
///
/// Deliberately NOT "what is being browsed" and NOT "what MPD's queue holds":
/// it is the collection an explicit playback start came from. Only that
/// collection may paint the playing row.
struct PlaybackCollection {
  enum class Kind { None, Library, History, Streams, Agent, Playlist };

  Kind kind = Kind::None;
  /// Playlist name; empty for every other kind.
  std::string name;

  bool operator==(const PlaybackCollection &other) const {
    return kind == other.kind && name == other.name;
  }
  bool operator!=(const PlaybackCollection &other) const {
    return !(*this == other);
  }
  bool valid() const { return kind != Kind::None; }
};

/// One playback run: the ordered snapshot taken when the user pressed Enter on
/// a row, plus where in it playback currently is.
///
/// The snapshot is a COPY. Editing the playlist, re-synchronising `default` or
/// deleting a History record afterwards does not mutate a run that is already
/// under way -- only the next explicit start takes a new snapshot.
struct PlaybackSession {
  PlaybackCollection context;
  std::vector<Song> sequence;
  /// Index into `sequence` of the occurrence that is playing.
  int occurrence = -1;
  /// `song.uri` + `song.playback_occurrence` of that same entry, so "which
  /// duplicate is playing" survives a re-scan of the collection.
  std::string uri;
  int uri_occurrence = 1;
  /// The History record the run started from, when the context is History.
  long long history_record_id = 0;
  /// The store epoch the History context was created in.
  long long history_epoch = 0;
  /// The MPD queue id playback is on, when the run owns the queue.
  int queue_id = -1;
  /// True when this run put its sequence into MPD's queue, so Pre/Next follow
  /// the context instead of whatever the queue happened to hold.
  bool owns_queue = false;

  bool valid() const { return context.valid() && !sequence.empty(); }
};

struct PlayerState {
  PlaybackState state = PlaybackState::Stopped;
  std::optional<Song> current_song;
  /// MPD snapshot; the UI interpolates from here with steady_clock.
  double elapsed_seconds = 0.0;
  double duration_seconds = 0.0;
  int volume = -1;
  bool repeat = false;
  bool random = false;
  int current_song_id = -1;
  std::chrono::steady_clock::time_point sampled_at{};
};

/// Elapsed seconds to draw right now: snapshot + interpolation while playing.
double effectiveElapsed(const PlayerState &player,
                        std::chrono::steady_clock::time_point now);

/// effectiveElapsed() / duration, clamped to [0, 1]; 0 when unknown.
double progressRatio(const PlayerState &player,
                     std::chrono::steady_clock::time_point now);

struct LibraryState {
  /// Index 0 is always the protected virtual Default playlist. The remaining
  /// entries are saved playlists read from MPD in MPD's order.
  std::vector<std::string> playlists;
  int current = 0;
  std::vector<Song> songs;
  std::vector<int> visible; // Indices of songs passing the search filter.
  int selected = 0;         // Index into `visible`.
  bool loading = false;
  std::string error;
  bool search_active = false; // True while the list holds MPD search results.
  /// True while `songs` holds the complete MPD media database (the Library
  /// collection, which is read-only) rather than a saved playlist.
  bool database_view = false;
};

/// Spectrum data. Phase 1-4 leaves the bars at zero: faking them with random
/// or sine values is explicitly forbidden (§10.2, §19).
struct VisualizerState {
  std::vector<float> bars;  // Normalised 0..1 target per band.
  std::vector<float> peaks; // Peak-hold overlay per band.
  /// Motion state. `position` is what the renderer draws; `velocity` carries
  /// momentum so bars spring up and glide down instead of tracking the target.
  /// Both persist across frames, page switches and resizes.
  std::vector<float> position;
  std::vector<float> velocity;
  std::vector<float> display; // unused legacy slot
  /// Two-timescale envelopes: `fast` carries transients, `slow` keeps the
  /// musical body between hits so bars do not collapse to the axis.
  std::vector<float> fast_envelope;
  std::vector<float> slow_envelope;
  /// Reflection temporal low-pass. Slightly stickier than the body so the
  /// mirror reads as water rather than a dark copy.
  std::vector<float> reflection;
  float sensitivity = 1.0f;
  bool data_available = false;
};

struct SettingsState {
  Page start_page = Page::Library;
  int seek_step = 5;
  int volume_step = 5;
  std::string library_path;
  std::string mpd_host;
  int mpd_port = 0;
  bool auto_reconnect = true;
  /// The socket timeout in milliseconds. Mirrors the effective value, so the
  /// Connection pane reports the contract the backend actually uses (never 0).
  int mpd_timeout_ms = 2000;
  std::string theme_name = "default";
  int visualizer_refresh_hz = 30;
  int visualizer_bar_density = 64;
  std::string visualizer_fifo = "/tmp/mpd.fifo";
  int visualizer_sample_rate = 44100;
  int visualizer_channels = 2;
};

struct AppState {
  Page page = Page::Library;
  /// Orthogonal to `page`: the immersive display replaces the main region of
  /// whatever section is active and returns to it unchanged.
  PresentationMode presentation = PresentationMode::Normal;
  DisplayMode display_mode = DisplayMode::Spectrum;
  /// The music register survives pane switches, collection changes and
  /// presentation changes until it is overwritten.
  MusicRegister music_register;
  bool mpd_connected = false;

  PlayerState player;

  std::vector<Song> queue;
  int selected_queue = 0;
  /// Indices into `queue` that pass the current search filter.
  std::vector<int> queue_visible;

  LibraryState library;
  SettingsState settings;
  VisualizerState visualizer;

  FocusArea focus = FocusArea::Library;

  /// Fatal-ish condition rendered in the main content area (e.g. MPD down).
  std::optional<std::string> error;
  /// Short-lived message shown in the top bar.
  std::optional<std::string> toast;
  std::chrono::steady_clock::time_point toast_at{};

  /// Filter applied to the browsed collection. It is entered explicitly with
  /// `/` and cleared with Esc; there is no widget that owns it, so it can never
  /// be set as a side effect of showing a page.
  std::string search_query;

  /// Reference mode (`--demo`): renders the fixed reference screenshot state
  /// instead of live MPD data so visual comparisons are reproducible.
  bool demo = false;

  // --- Pointer hover state, refreshed from mouse-motion events ---------------
  int hover_nav = -1;          // Top navigation tab under the cursor.
  int hover_row = -1;          // List row under the cursor, -1 when none.
  int hover_playlist = -1;     // Saved-playlist tab under the cursor.
  int hover_control = -1;      // Player-bar control index, -1 when none.
  bool hover_progress = false; // Cursor is over the progress slider.
  bool hover_volume = false;   // Cursor is over the volume slider.
};

/// Elapsed time to draw: interpolated live, but frozen on the reference
/// snapshot in demo mode so screenshots stay reproducible.
double displayElapsed(const AppState &state,
                      std::chrono::steady_clock::time_point now);

/// Progress ratio to draw, honouring demo mode.
double displayProgress(const AppState &state,
                       std::chrono::steady_clock::time_point now);

/// Fills `state` with the fixed reference queue/player/spectrum used by
/// `--demo`: 28 tracks, 搁浅 selected and playing at 2:17 of 4:15, volume 75.
void loadReferenceState(AppState &state);

/// Deterministic reference spectrum shape (high left, decaying right).
void loadReferenceSpectrum(VisualizerState &visualizer, std::size_t bands);

} // namespace termusic
