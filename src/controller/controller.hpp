#pragma once

#include <string>
#include <string_view>

#include "app/actions.hpp"
#include "app/diagnostics.hpp"
#include "app/history.hpp"
#include "app/keymap.hpp"
#include "app/state.hpp"
#include "backend/mpd_backend.hpp"
#include "config/config.hpp"

namespace termusic {

/// How long a reachability probe may take before an automatic retry gives up on
/// this attempt. Small on purpose: the UI thread runs it, and an unreachable
/// address must not be able to stall a frame (or `q`) for long.
inline constexpr int kReachabilityProbeMs = 300;

class Controller {
public:
  Controller(MpdBackend &backend, AppState &state, Config &config,
             ConfigStore &config_store);

  bool initialize();
  bool reconnect();

  /// The endpoint every connection attempt uses: config.toml with the
  /// environment and the command line applied on top, resolved once at startup
  /// (see resolveConnection). Deliberately SEPARATE from `config_`: a temporary
  /// `--host`/MPD_HOST override is used for this session and is never written
  /// back, so saving an unrelated setting cannot freeze it into the file.
  void useConnectionSettings(const ConnectionSettings &settings);
  /// Where failures are recorded. Optional: a null log is a no-op.
  void setDiagnosticsLog(const DiagnosticsLog *log) { diagnostics_ = log; }
  /// Stops MPD playback as part of an application-requested shutdown. This is
  /// intentionally separate from disconnect(): closing a client connection
  /// alone does not stop the MPD daemon or its audio output.
  void stopPlaybackForExit();
  void handleBackendEvent(BackendEvent changed);
  void execute(Action action);

  void refreshPlayer();
  void refreshQueue();
  void applyQueueFilter();
  void refreshPlaylists();
  void refreshLibrary();
  void searchCurrent(std::string_view query);
  void clearSearch();

  void selectPage(Page page);
  void selectPlaylist(int index);
  /// Makes the MPD media database the active Track Buffer source. This is the
  /// single authoritative path for selecting "All Songs"; it resets
  /// `library.current`, which is what `refreshLibrary()` keys off. Without
  /// that reset, selecting All Songs after a playlist reloaded the playlist.
  void selectDatabase();
  /// Starts playback of `song` and makes `context` the playback context.
  ///
  /// `tracks` is the collection's ordered contents -- the run is a snapshot of
  /// exactly this list, so Previous, Next and automatic progression follow it,
  /// and editing the collection later cannot rewrite what is already playing.
  void playTrack(const Song &song, const std::vector<Song> &tracks,
                 PlaybackCollection context);
  /// The same, addressed by row instead of by value. Used by the UI, whose
  /// cursor is the row.
  void playTrackAt(const std::vector<Song> &tracks, int index,
                   PlaybackCollection context);
  /// The active playback run, for the UI's marker decision. `sequence` is
  /// empty when no context is known (startup, external change), which is what
  /// makes every collection show zero markers.
  const PlaybackSession &playbackSession() const { return session_; }
  /// The collection that owns playback right now.
  const PlaybackCollection &playbackContext() const {
    return session_.context;
  }

  /// Removes History records by id. Application-owned storage only: no file,
  /// no MPD database entry, no saved playlist and no queue item is touched, and
  /// an in-flight playback run keeps its own snapshot. Returns how many were
  /// removed, or -1 when persistence failed.
  int removeHistoryRecords(const std::vector<long long> &ids);
  void selectLibrarySong(int index);
  void selectQueueSong(int index);
  void activateLibrarySelection();
  void removeSelectedFromQueue();

  void seekAbsolute(double seconds);
  void setVolume(int volume);
  void setSeekStep(int seconds);
  void setVolumeStep(int percent);
  void setStartPage(Page page);
  /// [general] stop_on_exit: whether quitting also stops MPD playback.
  void setStopOnExit(bool stop);
  void setVisualizerSensitivity(float sensitivity);
  void setVisualizerRefreshHz(int refresh_hz);
  void setVisualizerDensity(int density);
  /// Selects the colour ramp the Spectrum is drawn in.
  void setVisualizerPalette(const std::string &palette);
  /// Core -> Connection's "Save and reconnect": the ONE place that persists an
  /// endpoint, because the user typed it and pressed the button.
  void setMpdConnection(std::string host, int port, std::string password,
                        bool auto_reconnect);

  bool createPlaylist(std::string_view name);
  bool renameCurrentPlaylist(std::string_view name);
  bool deleteCurrentPlaylist();
  bool addSelectedToPlaylist(int playlist_index);

  /// The application-owned playback history.
  const HistoryStore &history() const { return history_; }

  /// Phase C paste. Appends the music register in order and returns how many
  /// tracks were actually appended. A failed paste never clears the register.
  int pasteRegisterToQueue();

  /// Phase 45 entry deletion. Queue entries are addressed by their stable
  /// queue id; playlist entries are positional, so callers pass positions and
  /// the backend removes them highest-first.
  int removeQueueEntries(const std::vector<unsigned> &queue_ids);
  int removePlaylistEntries(std::string_view name,
                            const std::vector<unsigned> &positions);
  int pasteRegisterToPlaylist(std::string_view name);
  bool moveCurrentPlaylistItem(int position, int delta);
  /// Persists one keybinding override. Only the touched entry is written, so
  /// unrelated configuration (and other keybindings) survive untouched.
  bool bindKey(KeyContext context, Action action,
               const std::vector<std::string> &sequences);
  /// True when `collection` still exists, so a context can be invalidated
  /// instead of pointing at a collection that is gone.
  bool collectionExists(const PlaybackCollection &collection) const;
  /// The row in `tracks` that the active run is playing, or -1. Only the owner
  /// collection may answer, and the answer is keyed on the OCCURRENCE, never on
  /// the URI alone.
  int playingRow(const PlaybackCollection &collection,
                 const std::vector<Song> &tracks) const;
  /// Re-reads MPD's player state into the active run: follows an automatic
  /// advance, and gives up the context when playback moved somewhere the run
  /// does not describe (an external client, a deleted playlist).
  void syncPlaybackSession();
  /// Removes one override, so the built-in default applies again.
  bool unbindKey(KeyContext context, Action action);
  /// Forgets one loaded override IN MEMORY, without writing the file. Used when
  /// an entry is no longer configurable: the obsolete key is dropped so the
  /// next save stops writing it, while startup never rewrites the user's
  /// configuration behind their back.
  bool forgetKeyOverride(KeyContext context, Action action);
  void resetKeymap();
  bool saveConfig();

  const Config &config() const { return config_; }
  Config &config() { return config_; }
  /// The configuration file this session reads and writes, for the Help
  /// module's "config" line.
  const std::filesystem::path &configPath() const {
    return config_store_.path();
  }
  /// The endpoint in use, for the Connection pane and the diagnostics.
  const ConnectionSettings &connection() const { return connection_; }
  /// Bumped whenever the endpoint is (re)configured -- at startup and by
  /// "Save and reconnect". The retry ladder watches this: a NEW endpoint starts
  /// its backoff from the beginning instead of inheriting the old one's.
  std::size_t connectionEpoch() const { return connection_epoch_; }

private:
  const Song *selectedSong() const;
  const std::vector<Song> &historySongs() const;
  const Song *selectedQueueSong() const;
  /// Finds `target` inside `tracks` by URI *and* duplicate count, so the second
  /// A in `A B A C` is addressable. Returns -1 when it is not there.
  int occurrenceOf(const std::vector<Song> &tracks, const Song &target) const;
  /// Moves `delta` steps inside the active run's snapshot, wrapping at its
  /// ends. Never leaves the context.
  void stepContext(int delta);

  void moveSelection(int delta);
  void moveSelectionTo(bool last);
  void movePlaylist(int delta);
  void moveQueueItem(int delta);
  /// Records a playback occurrence when the authoritative player state shows
  /// a genuinely NEW one. Called from refreshPlayer(), so automatic MPD
  /// transitions are captured too -- not just the UI's Enter action.
  void maybeRecordHistory();
  void reportResult(bool success, std::string_view success_message = {});
  void toast(std::string message);
  void syncSettingsState();

  MpdBackend &backend_;
  AppState &state_;
  Config &config_;
  ConfigStore &config_store_;
  HistoryStore history_;
  /// Effective endpoint for this session. Initialized from the stored config so
  /// a Controller used without `useConnectionSettings()` (the tests) behaves
  /// exactly like the real one.
  ConnectionSettings connection_;
  /// Incremented on every endpoint change (see connectionEpoch()).
  std::size_t connection_epoch_ = 0;
  const DiagnosticsLog *diagnostics_ = nullptr;
  /// Queue id of the occurrence currently being counted, so pause / resume /
  /// seek / volume changes cannot append the same playback twice. Unset means
  /// "no occurrence observed yet", which is what keeps a launch that attaches
  /// to an already-playing MPD from inventing an entry.
  std::optional<int> history_identity_;
  /// False until the first player sample after a (re)connect has been taken.
  /// It is what distinguishes "this song was already playing when termusic
  /// attached" (adopt, do not record) from "nothing was playing, and then a
  /// song started" (record).
  bool history_primed_ = false;
  PlaybackState history_last_state_ = PlaybackState::Stopped;
  /// The collection playback started from, and the snapshot it is playing.
  /// Empty context + empty sequence = "unknown / external", which shows no
  /// markers anywhere and never guesses one from a URI.
  PlaybackSession session_;
  /// `uri` + occurrence of the last observed current song, so a repeat of the
  /// same queue entry is not mistaken for an advance.
  std::string observed_uri_;
  int observed_occurrence_ = 0;
  int observed_song_id_ = -1;
  /// Cached newest-first view for callers that need it; see historySongs().
  mutable std::vector<Song> history_cache_;
  mutable std::size_t history_cache_revision_ = static_cast<std::size_t>(-1);
};

} // namespace termusic
