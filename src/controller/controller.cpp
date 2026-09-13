#include "controller/controller.hpp"

#include "app/playback.hpp"


#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>

#include "ui/visualizer/palette.hpp"
#include "util/text.hpp"

namespace termusic {

Controller::Controller(MpdBackend &backend, AppState &state, Config &config,
                       ConfigStore &config_store)
    : backend_(backend), state_(state), config_(config),
      config_store_(config_store) {
  syncSettingsState();
  state_.page = config_.start_page;
  state_.focus = state_.page == Page::Library
                     ? FocusArea::Library
                     : FocusArea::Settings;
  state_.visualizer.sensitivity = config_.visualizer_sensitivity;
  // Until main() hands over the resolved endpoint, the stored configuration is
  // the best answer available.
  connection_.host = config_.mpd_host;
  connection_.port = config_.mpd_port;
  connection_.password = config_.mpd_password;
  connection_.timeout_ms = config_.mpd_timeout_ms;
  connection_.auto_reconnect = config_.auto_reconnect;
  // History is application state, independent of MPD: load it before the
  // first connection so it survives a backend that is down. A user who turned
  // history off gets neither a read nor a write -- the file is left alone.
  history_.setMaxEntries(static_cast<std::size_t>(
      std::max(1, config_.history_max_entries)));
  if (config_.history_enabled)
    (void)history_.load();
}

void Controller::useConnectionSettings(const ConnectionSettings &settings) {
  connection_ = settings;
  ++connection_epoch_;
  // Mirror the endpoint into the live state so the Connection pane shows what
  // is actually being used, overrides included.
  state_.settings.mpd_host = settings.host;
  state_.settings.mpd_port = settings.port;
  state_.settings.auto_reconnect = settings.auto_reconnect;
  state_.settings.mpd_timeout_ms = settings.timeout_ms;
}

namespace {
/// A concise, actionable reason for a failed connection -- never a raw
/// libmpdclient dump, never a password.
std::string conciseConnectionError(const std::string &backend_error) {
  if (backend_error.empty())
    return "Disconnected";
  return backend_error;
}
} // namespace

bool Controller::initialize() {
  if (state_.demo) {
    // Reference mode: publish the fixed screenshot state and leave MPD alone.
    loadReferenceState(state_);
    return true;
  }
  return reconnect();
}

bool Controller::reconnect() {
  if (state_.demo) {
    loadReferenceState(state_);
    return true;
  }
  MpdConnectionOptions options;
  options.host = connection_.host;
  options.port = static_cast<unsigned>(std::max(0, connection_.port));
  options.password = connection_.password;
  // Bounded, non-destructive reachability probe first: an address that does not
  // answer cannot make the caller wait for the whole socket timeout, so the UI
  // stays responsive while automatic retries keep coming. The probe sends no
  // MPD command and changes nothing; when it fails the reason is the same
  // concise phrase a real attempt would have produced.
  if (!backend_.reachable(options, kReachabilityProbeMs)) {
    if (state_.mpd_connected) {
      state_.mpd_connected = false;
      state_.player = PlayerState{};
      state_.queue.clear();
      state_.library.songs.clear();
    }
    state_.error = "Connection refused";
    if (diagnostics_ != nullptr)
      diagnostics_->write("mpd: " + connection_.host + ":" +
                          std::to_string(connection_.port) +
                          " did not answer a " +
                          std::to_string(kReachabilityProbeMs) +
                          "ms probe; not attempting a full connect");
    return false;
  }
  // Never zero: see kDefaultMpdTimeoutMs. resolveConnection() already
  // guarantees a sane value, and this is the second line of defence.
  options.timeout_ms = static_cast<unsigned>(
      connection_.timeout_ms > 0 ? connection_.timeout_ms : kDefaultMpdTimeoutMs);
  state_.mpd_connected = backend_.connect(options);
  if (!state_.mpd_connected) {
    state_.error = conciseConnectionError(backend_.lastError());
    if (diagnostics_ != nullptr)
      diagnostics_->write("mpd: connect to " + connection_.host + ":" +
                          std::to_string(connection_.port) + " failed: " +
                          state_.error.value_or("unknown error"));
    state_.player = PlayerState{};
    state_.queue.clear();
    state_.library.songs.clear();
    return false;
  }
  if (diagnostics_ != nullptr)
    diagnostics_->write("mpd: connected to " + connection_.host + ":" +
                        std::to_string(connection_.port));

  state_.error.reset();
  // A new connection starts a new observation window: whatever is already
  // playing is adopted, not recorded.
  history_primed_ = false;
  refreshPlayer();
  refreshQueue();
  refreshPlaylists();
  // Connecting is OBSERVATIONAL: status, current song, queue, database and
  // playlist names are read, and nothing on the server is written. In
  // particular no saved playlist is created, cleared, rebuilt or deleted -- a
  // generic name like "default" belongs to the user.
  refreshLibrary();
  return true;
}

void Controller::stopPlaybackForExit() {
  // [general] stop_on_exit = true (the default) keeps the historical contract:
  // quitting stops MPD playback, because disconnecting the client alone would
  // leave the daemon playing. With stop_on_exit = false the user asked for the
  // other behaviour explicitly: termusic only closes its connection and MPD
  // keeps playing.
  if (state_.demo)
    return;
  if (!config_.stop_on_exit) {
    // Deliberately different from stopPlaybackForExit(): the UI state still
    // leaves the "playing" view, but the daemon is not told to stop.
    backend_.disconnect();
    return;
  }
  // Ask the backend unconditionally in live mode: AppState is a UI snapshot
  // and may lag a just-established or just-failed connection. MpdBackend::stop
  // already treats a missing command connection as a harmless failure.
  (void)backend_.stop();
  state_.player.state = PlaybackState::Stopped;
}

void Controller::handleBackendEvent(BackendEvent changed) {
  if (hasEvent(changed, BackendEvent::Player) ||
      hasEvent(changed, BackendEvent::Mixer) ||
      hasEvent(changed, BackendEvent::Options)) {
    refreshPlayer();
  }
  if (hasEvent(changed, BackendEvent::Queue))
    refreshQueue();
  if (hasEvent(changed, BackendEvent::Database)) {
    // The media database changed. Refresh Library if it is the displayed
    // source, then re-mirror `default` -- this is the authoritative
    // "database content actually changed" trigger.
    if (state_.library.database_view)
      refreshLibrary();
  }
  if (hasEvent(changed, BackendEvent::Update)) {
    toast("MPD database update changed state");
  }
  state_.mpd_connected = backend_.connected();
  if (!state_.mpd_connected)
    state_.error = backend_.lastError();
}

void Controller::execute(Action action) {
  switch (action) {
  case Action::PageLibrary:
    selectPage(Page::Library);
    break;
  case Action::PageSettings:
    selectPage(Page::Settings);
    break;
  case Action::Help:
    // The help overlay is owned by the UI layer; nothing to do here.
    break;
  case Action::FocusNext:
    state_.focus = state_.focus == FocusArea::PlayerBar
                       ? (state_.page == Page::Library ? FocusArea::Library
                                                      : FocusArea::Settings)
                       : FocusArea::PlayerBar;
    break;
  case Action::TogglePlay: {
    bool result = false;
    switch (state_.player.state) {
    case PlaybackState::Playing:
      result = backend_.pause(true);
      break;
    case PlaybackState::Paused:
      result = backend_.pause(false);
      break;
    case PlaybackState::Stopped:
      result = backend_.play();
      break;
    }
    reportResult(result);
    if (result)
      refreshPlayer();
    break;
  }
  case Action::Next:
  case Action::Previous:
    // One rule for every transport entry point (keymap, player bar, immersive):
    // skip inside the PLAYBACK CONTEXT's snapshot. MPD's own `next` is only the
    // fallback for "no context known" (startup, external playback), because an
    // unrelated queue order must never be presented as the user's playlist.
    stepContext(action == Action::Next ? 1 : -1);
    break;
  case Action::SeekForward:
    reportResult(backend_.seekRelative(config_.seek_step));
    if (state_.mpd_connected)
      refreshPlayer();
    break;
  case Action::SeekBackward:
    reportResult(backend_.seekRelative(-config_.seek_step));
    if (state_.mpd_connected)
      refreshPlayer();
    break;
  case Action::VolumeUp:
    setVolume(state_.player.volume < 0
                  ? config_.volume_step
                  : state_.player.volume + config_.volume_step);
    break;
  case Action::VolumeDown:
    setVolume(state_.player.volume < 0
                  ? 0
                  : state_.player.volume - config_.volume_step);
    break;
  case Action::ToggleRepeat:
    reportResult(backend_.setRepeat(!state_.player.repeat));
    refreshPlayer();
    break;
  case Action::ToggleShuffle:
    reportResult(backend_.setRandom(!state_.player.random));
    refreshPlayer();
    break;
  case Action::Reconnect:
    reconnect();
    break;
  case Action::UpdateDatabase:
    reportResult(backend_.updateDatabase(), "Database update submitted");
    break;
  case Action::MoveDown:
    moveSelection(1);
    break;
  case Action::MoveUp:
    moveSelection(-1);
    break;
  case Action::MoveToFirst:
    moveSelectionTo(false);
    break;
  case Action::MoveToLast:
    moveSelectionTo(true);
    break;
  case Action::HalfPageDown:
    moveSelection(8);
    break;
  case Action::HalfPageUp:
    moveSelection(-8);
    break;
  case Action::Activate:
    // The selected collection row is the only thing Activate can mean: the
    // workspace owns the cursor, so the Controller needs no focus test.
    if (state_.page == Page::Library)
      activateLibrarySelection();
    break;
  case Action::GoParent:
    // There is no directory level to climb any more: `h` on a collection is
    // the seek-backward key it has always been in the player context.
    execute(Action::SeekBackward);
    break;
  case Action::EnterDirectory:
    execute(Action::SeekForward);
    break;
  case Action::PrevPlaylist:
    movePlaylist(-1);
    break;
  case Action::NextPlaylist:
    movePlaylist(1);
    break;
  case Action::ToggleQueue:
    // The runtime queue is not a collection any more, so this action can only
    // put the workspace back on the media database. It has no default binding.
    selectPage(Page::Library);
    break;
  case Action::AddToQueue:
    if (const Song *song = selectedSong(); song != nullptr) {
      reportResult(backend_.add(song->uri) >= 0, "Added to the queue");
      refreshQueue();
    }
    break;
  case Action::RemoveSelected:
    removeSelectedFromQueue();
    break;
  case Action::ClearQueue:
    reportResult(backend_.clearQueue(), "Queue cleared");
    refreshQueue();
    break;
  case Action::MoveQueueItemUp:
    moveQueueItem(-1);
    break;
  case Action::MoveQueueItemDown:
    moveQueueItem(1);
    break;
  case Action::LoadPlaylistToQueue:
    // `database_view` -- not the index -- says whether a SAVED playlist is the
    // displayed source: index 0 is a real playlist like every other one.
    if (!state_.library.database_view && state_.library.current >= 0 &&
        state_.library.current <
            static_cast<int>(state_.library.playlists.size())) {
      reportResult(
          backend_.loadPlaylistToQueue(
              state_.library
                  .playlists[static_cast<std::size_t>(state_.library.current)]),
          "Playlist loaded into the queue");
      refreshQueue();
      refreshPlayer();
    }
    break;
  default:
    break;
  }
}

void Controller::refreshPlayer() {
  if (!backend_.connected())
    return;
  state_.player = backend_.fetchPlayerState();
  reportResult(backend_.connected());
  maybeRecordHistory();
  // Order matters: the history record for a NEW occurrence must exist before
  // the session tries to name it, and following an automatic advance must not
  // depend on which collection is being browsed.
  syncPlaybackSession();
}

void Controller::maybeRecordHistory() {
  const PlayerState &player = state_.player;
  const bool has_song = player.current_song.has_value() &&
                        player.current_song_id >= 0 &&
                        !player.current_song->uri.empty();
  if (!has_song) {
    // Nothing is loaded: the next song that starts is a new occurrence.
    history_identity_.reset();
    history_last_state_ = player.state;
    history_primed_ = true;
    return;
  }

  const bool record = [&] {
    if (!history_primed_) {
      // Very first sample after attaching to MPD. The song may have been
      // playing long before termusic started, so it is ADOPTED as the baseline
      // rather than recorded -- otherwise every launch would add an entry for
      // whatever happened to be playing.
      history_primed_ = true;
      history_identity_ = player.current_song_id;
      history_last_state_ = player.state;
      return false;
    }
    if (!history_identity_.has_value()) {
      // Nothing was playing when we last looked, so this is a genuine start
      // that termusic observed from the beginning.
      history_primed_ = true;
      history_identity_ = player.current_song_id;
      history_last_state_ = player.state;
      return true;
    }
    const bool new_song = player.current_song_id != *history_identity_;
    // A fresh start of the SAME queue entry (stop, then play again) is a new
    // occurrence too. Pause -> resume is Paused -> Playing and is deliberately
    // NOT one, so it cannot duplicate the entry.
    const bool restarted = player.state == PlaybackState::Playing &&
                           history_last_state_ == PlaybackState::Stopped &&
                           !new_song;
    history_last_state_ = player.state;
    if (!new_song && !restarted)
      return false;
    history_identity_ = player.current_song_id;
    return true;
  }();

  if (!record)
    return;
  history_.record(*player.current_song);
  std::string error;
  if (!history_.save(&error))
    toast("History not saved: " + error);
}

const std::vector<Song> &Controller::historySongs() const {
  if (history_cache_revision_ != history_.revision()) {
    history_cache_ = history_.songsNewestFirst();
    history_cache_revision_ = history_.revision();
  }
  return history_cache_;
}

bool Controller::moveCurrentPlaylistItem(int position, int delta) {
  const int current = state_.library.current;
  // A saved playlist is user-owned whatever it is called, so reordering is
  // allowed for every one of them; only a non-playlist collection is refused.
  if (state_.library.database_view || current < 0 ||
      current >= static_cast<int>(state_.library.playlists.size()))
    return false;
  const std::string name =
      state_.library.playlists[static_cast<std::size_t>(current)];
  const int count = static_cast<int>(state_.library.songs.size());
  if (count < 2)
    return false;
  const int from = std::clamp(position, 0, count - 1);
  const int to = std::clamp(from + delta, 0, count - 1);
  if (from == to)
    return false;
  const bool result =
      backend_.moveInPlaylist(name, static_cast<unsigned>(from),
                              static_cast<unsigned>(to));
  reportResult(result, "Reordered \"" + name + "\"");
  if (result) {
    state_.library.selected = to;
    refreshLibrary();
  }
  return result;
}

void Controller::refreshQueue() {
  if (!backend_.connected())
    return;
  state_.queue = backend_.fetchQueue();
  applyQueueFilter();
  reportResult(backend_.connected());
}

void Controller::applyQueueFilter() {
  state_.queue_visible.clear();
  state_.queue_visible.reserve(state_.queue.size());
  for (std::size_t index = 0; index < state_.queue.size(); ++index) {
    if (state_.search_query.empty()) {
      state_.queue_visible.push_back(static_cast<int>(index));
      continue;
    }
    const Song &song = state_.queue[index];
    if (util::containsIgnoreCase(song.displayTitle(), state_.search_query) ||
        util::containsIgnoreCase(song.artist, state_.search_query) ||
        util::containsIgnoreCase(song.album, state_.search_query)) {
      state_.queue_visible.push_back(static_cast<int>(index));
    }
  }
  state_.selected_queue =
      std::clamp(state_.selected_queue, 0,
                 std::max(0, static_cast<int>(state_.queue_visible.size()) - 1));
}

void Controller::refreshPlaylists() {
  if (!backend_.connected())
    return;
  // The list is EXACTLY what MPD reports: every saved playlist, whatever it is
  // called, and nothing else. MPD's runtime queue is not a playlist and gets no
  // entry here, so a user playlist called "default" is listed once and is never
  // shadowed, renamed or rewritten by the application.
  std::string selected;
  if (state_.library.current >= 0 &&
      state_.library.current <
          static_cast<int>(state_.library.playlists.size())) {
    selected = state_.library.playlists[static_cast<std::size_t>(
        state_.library.current)];
  }
  state_.library.playlists = backend_.listPlaylists();
  const auto found = std::find(state_.library.playlists.begin(),
                               state_.library.playlists.end(), selected);
  state_.library.current =
      found == state_.library.playlists.end()
          ? 0
          : static_cast<int>(
                std::distance(state_.library.playlists.begin(), found));
}

void Controller::refreshLibrary() {
  if (!backend_.connected())
    return;
  state_.library.loading = true;
  if (state_.library.database_view) {
    state_.library.songs = backend_.fetchAllSongs();
  } else if (!state_.library.playlists.empty()) {
    state_.library.current = std::clamp(
        state_.library.current, 0,
        static_cast<int>(state_.library.playlists.size()) - 1);
    state_.library.songs = backend_.listPlaylist(
        state_.library
            .playlists[static_cast<std::size_t>(state_.library.current)]);
  }
  state_.library.loading = false;
  state_.library.search_active = false;
  state_.library.visible.resize(state_.library.songs.size());
  for (std::size_t index = 0; index < state_.library.songs.size(); ++index) {
    state_.library.visible[index] = static_cast<int>(index);
  }
  state_.library.selected = std::clamp(
      state_.library.selected, 0,
      std::max(0, static_cast<int>(state_.library.visible.size()) - 1));
  if (!backend_.connected())
    state_.library.error = backend_.lastError();
  else
    state_.library.error.clear();
}

void Controller::searchCurrent(std::string_view query) {
  state_.search_query = std::string(query);
  // The saved-playlist mirror is filtered in lockstep with the collection the
  // Library shows, so `n`/`N` never jump to a row the filter removed.
  if (state_.page == Page::Library)
    applyQueueFilter();
}

void Controller::clearSearch() {
  const bool was_searching = !state_.search_query.empty();
  state_.search_query.clear();
  if (state_.page == Page::Library && was_searching)
    applyQueueFilter();
}

int Controller::pasteRegisterToQueue() {
  if (state_.music_register.empty())
    return 0;
  int added = 0;
  // Register order is preserved exactly: the queue grows by A, B, C in that
  // order, and nothing already queued is replaced or reordered.
  for (const TrackRef &ref : state_.music_register.tracks) {
    if (ref.uri.empty())
      continue;
    if (backend_.add(ref.uri) >= 0)
      ++added;
  }
  if (added > 0)
    refreshQueue();
  return added;
}

int Controller::removeQueueEntries(const std::vector<unsigned> &queue_ids) {
  int removed = 0;
  // Stable ids, so order does not matter and playback identity is untouched.
  for (const unsigned id : queue_ids) {
    if (backend_.remove(id))
      ++removed;
  }
  if (removed > 0)
    refreshQueue();
  return removed;
}

int Controller::removePlaylistEntries(std::string_view name,
                                      const std::vector<unsigned> &positions) {
  if (name.empty())
    return 0;
  int removed = 0;
  // The backend addresses playlist entries by position, so deleting from the
  // front would shift every later index. Normalize arbitrary caller order and
  // remove duplicates before deleting highest-first.
  std::vector<unsigned> ordered = positions;
  std::sort(ordered.begin(), ordered.end());
  ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
  for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
    if (backend_.removeFromPlaylist(name, *it))
      ++removed;
  }
  if (removed > 0)
    refreshPlaylists();
  return removed;
}

int Controller::pasteRegisterToPlaylist(std::string_view name) {
  if (state_.music_register.empty() || name.empty())
    return 0;
  int added = 0;
  for (const TrackRef &ref : state_.music_register.tracks) {
    if (ref.uri.empty())
      continue;
    if (backend_.addToPlaylist(name, ref.uri))
      ++added;
  }
  if (added > 0)
    refreshPlaylists();
  return added;
}

void Controller::selectPage(Page page) {
  state_.page = page;
  // Returning from Settings must preserve the collection already open in the
  // Library workspace. Page selection does not imply Default selection.
  state_.focus = page == Page::Settings ? FocusArea::Settings
                                        : FocusArea::Library;
  clearSearch();
}

void Controller::selectDatabase() {
  state_.library.current = 0;
  state_.library.selected = 0;
  state_.library.database_view = true;
  state_.focus = FocusArea::Library;
  state_.search_query.clear();
  refreshLibrary();
}


void Controller::selectPlaylist(int index) {
  if (state_.library.playlists.empty())
    return;
  state_.library.current = std::clamp(
      index, 0, static_cast<int>(state_.library.playlists.size()) - 1);
  state_.library.selected = 0;
  state_.library.database_view = false;
  state_.focus = FocusArea::Library;
  state_.search_query.clear();
  refreshLibrary();
}

void Controller::selectLibrarySong(int index) {
  if (state_.library.visible.empty())
    return;
  state_.library.selected =
      std::clamp(index, 0, static_cast<int>(state_.library.visible.size()) - 1);
}

void Controller::selectQueueSong(int index) {
  if (state_.queue_visible.empty())
    return;
  state_.selected_queue =
      std::clamp(index, 0, static_cast<int>(state_.queue_visible.size()) - 1);
}

void Controller::activateLibrarySelection() {
  // The selected row is the playback START, and its collection becomes the
  // playback context.
  if (state_.library.visible.empty() || !state_.library.database_view)
    return;
  const int visible = std::clamp(
      state_.library.selected, 0,
      static_cast<int>(state_.library.visible.size()) - 1);
  const int row = state_.library.visible[static_cast<std::size_t>(visible)];
  PlaybackCollection context;
  context.kind = PlaybackCollection::Kind::Library;
  playTrackAt(state_.library.songs, row, std::move(context));
}

void Controller::seekAbsolute(double seconds) {
  reportResult(backend_.seekAbsolute(seconds));
  refreshPlayer();
}

void Controller::setVolume(int volume) {
  const int clamped = std::clamp(volume, 0, 100);
  reportResult(backend_.setVolume(static_cast<unsigned>(clamped)));
  refreshPlayer();
}

void Controller::setSeekStep(int seconds) {
  config_.seek_step = std::clamp(seconds, 1, 60);
  syncSettingsState();
  saveConfig();
}
void Controller::setVolumeStep(int percent) {
  config_.volume_step = std::clamp(percent, 1, 25);
  syncSettingsState();
  saveConfig();
}
void Controller::setStopOnExit(bool stop) {
  config_.stop_on_exit = stop;
  syncSettingsState();
  saveConfig();
}

void Controller::setStartPage(Page page) {
  config_.start_page = page;
  syncSettingsState();
  saveConfig();
}
void Controller::setVisualizerSensitivity(float sensitivity) {
  config_.visualizer_sensitivity = std::clamp(sensitivity, 0.1F, 5.0F);
  state_.visualizer.sensitivity = config_.visualizer_sensitivity;
  saveConfig();
}
void Controller::setVisualizerRefreshHz(int refresh_hz) {
  config_.visualizer_refresh_hz = std::clamp(refresh_hz, 5, 60);
  syncSettingsState();
  saveConfig();
}
void Controller::setVisualizerDensity(int density) {
  config_.visualizer_bar_density = std::clamp(density, 32, 96);
  syncSettingsState();
  saveConfig();
}
void Controller::setVisualizerPalette(const std::string &palette) {
  config_.visualizer_palette =
      std::string(ui::normalizeVisualizerPaletteId(palette));
  saveConfig();
}
void Controller::setMpdConnection(std::string host, int port,
                                  std::string password, bool auto_reconnect) {
  // Validation before anything is applied: a nonsense port is refused with a
  // message instead of being written and attempted.
  if (port < 1 || port > 65535) {
    toast("Port must be between 1 and 65535");
    return;
  }
  std::string trimmed_host = host;
  while (!trimmed_host.empty() &&
         std::isspace(static_cast<unsigned char>(trimmed_host.back())) != 0)
    trimmed_host.pop_back();
  const auto first = trimmed_host.find_first_not_of(" \t");
  if (first != std::string::npos)
    trimmed_host = trimmed_host.substr(first);
  if (trimmed_host.empty()) {
    // An empty host means "not configured here": the environment and then the
    // built-in default apply, exactly as they do for a missing key.
    trimmed_host = std::string(kDefaultMpdHost);
  }
  config_.mpd_host = trimmed_host;
  config_.mpd_port = port;
  config_.mpd_password = std::move(password);
  config_.auto_reconnect = auto_reconnect;
  connection_.host = config_.mpd_host;
  connection_.port = config_.mpd_port;
  connection_.password = config_.mpd_password;
  connection_.timeout_ms = config_.mpd_timeout_ms;
  connection_.auto_reconnect = config_.auto_reconnect;
  connection_.host_source = "Core > General";
  connection_.port_source = "Core > General";
  connection_.password_source = "Core > General";
  // A new endpoint: the automatic retry ladder starts over.
  ++connection_epoch_;
  syncSettingsState();
  // Persist first, then reconnect: the file and the live connection then agree,
  // and a failed connection leaves the saved setting in place for the next run.
  saveConfig();
  reconnect();
}

bool Controller::createPlaylist(std::string_view name) {
  // "Library" stays reserved because the tree already has a node with that
  // label. Nothing else is: "default" is an ordinary user name.
  if (name.empty() || name == "Library") {
    toast("Playlist name is invalid");
    return false;
  }
  const bool result = backend_.createPlaylist(name);
  reportResult(result, "Playlist created");
  if (result) {
    // Creation is a backend/Tree mutation only. It deliberately does NOT
    // select the new playlist: selecting counts as opening a collection, so
    // the Track Buffer would silently switch to a playlist the user only
    // asked to create. `library.current` and `library.songs` are untouched,
    // which keeps the displayed collection, the track cursor, the scroll
    // position and the focused pane exactly as they were.
    refreshPlaylists();
  }
  return result;
}

bool Controller::renameCurrentPlaylist(std::string_view name) {
  if (state_.library.database_view || state_.library.current < 0 ||
      state_.library.current >=
          static_cast<int>(state_.library.playlists.size()) ||
      name.empty() || name == "Library")
    return false;
  const std::string old_name =
      state_.library
          .playlists[static_cast<std::size_t>(state_.library.current)];
  const bool result = backend_.renamePlaylist(old_name, name);
  reportResult(result, "Playlist renamed");
  if (result) {
    refreshPlaylists();
    const auto found = std::find(state_.library.playlists.begin(),
                                 state_.library.playlists.end(), name);
    if (found != state_.library.playlists.end()) {
      state_.library.current = static_cast<int>(
          std::distance(state_.library.playlists.begin(), found));
    }
    refreshLibrary();
  }
  return result;
}

bool Controller::deleteCurrentPlaylist() {
  if (state_.library.database_view || state_.library.current < 0 ||
      state_.library.current >=
          static_cast<int>(state_.library.playlists.size())) {
    toast("Open a saved playlist first");
    return false;
  }
  const std::string name =
      state_.library
          .playlists[static_cast<std::size_t>(state_.library.current)];
  const bool result = backend_.deletePlaylist(name);
  reportResult(result, "Playlist deleted");
  if (result) {
    state_.library.current = 0;
    refreshPlaylists();
    refreshLibrary();
  }
  return result;
}

bool Controller::addSelectedToPlaylist(int playlist_index) {
  const Song *song = selectedSong();
  if (song == nullptr || playlist_index < 0 ||
      playlist_index >= static_cast<int>(state_.library.playlists.size())) {
    toast("Choose a saved playlist first");
    return false;
  }
  const bool result = backend_.addToPlaylist(
      state_.library.playlists[static_cast<std::size_t>(playlist_index)],
      song->uri);
  reportResult(result, "Added to playlist");
  return result;
}

bool Controller::bindKey(KeyContext context, Action action,
                         const std::vector<std::string> &sequences) {
  if (action == Action::None || sequences.empty())
    return false;
  config_.keybindings[std::string(contextName(context))]
                     [std::string(actionId(action))] = sequences;
  return saveConfig();
}

bool Controller::unbindKey(KeyContext context, Action action) {
  const auto table =
      config_.keybindings.find(std::string(contextName(context)));
  if (table == config_.keybindings.end())
    return false;
  if (table->second.erase(std::string(actionId(action))) == 0)
    return false;
  // Do not leave an empty table behind in the user's file.
  if (table->second.empty())
    config_.keybindings.erase(table);
  return saveConfig();
}

bool Controller::forgetKeyOverride(KeyContext context, Action action) {
  const auto table =
      config_.keybindings.find(std::string(contextName(context)));
  if (table == config_.keybindings.end())
    return false;
  if (table->second.erase(std::string(actionId(action))) == 0)
    return false;
  // Never leave an empty table behind in the in-memory configuration either:
  // an empty table would write a bare section header on the next save.
  if (table->second.empty())
    config_.keybindings.erase(table);
  return true;
}

void Controller::resetKeymap() {
  config_.keybindings.clear();
  saveConfig();
  toast("Default Vim keymap restored");
}

bool Controller::saveConfig() {
  syncSettingsState();
  std::string error;
  if (!config_store_.save(config_, &error)) {
    toast("Config: " + error);
    return false;
  }
  return true;
}

const Song *Controller::selectedSong() const {
  if (state_.page != Page::Library)
    return nullptr;
  if (state_.library.visible.empty())
    return nullptr;
  const int visible_index =
      std::clamp(state_.library.selected, 0,
                 static_cast<int>(state_.library.visible.size()) - 1);
  const int song_index =
      state_.library.visible[static_cast<std::size_t>(visible_index)];
  if (song_index < 0 ||
      song_index >= static_cast<int>(state_.library.songs.size()))
    return nullptr;
  return &state_.library.songs[static_cast<std::size_t>(song_index)];
}

const Song *Controller::selectedQueueSong() const {
  if (state_.queue_visible.empty())
    return nullptr;
  const int index = std::clamp(state_.selected_queue, 0,
                               static_cast<int>(state_.queue_visible.size()) - 1);
  const int queue_index =
      state_.queue_visible[static_cast<std::size_t>(index)];
  if (queue_index < 0 || queue_index >= static_cast<int>(state_.queue.size()))
    return nullptr;
  return &state_.queue[static_cast<std::size_t>(queue_index)];
}

void Controller::moveSelection(int delta) {
  // The selected collection is the only list the Controller moves through:
  // Tree and Track List share this cursor, and the workspace clamps it.
  if (state_.page != Page::Library || state_.library.visible.empty())
    return;
  state_.library.selected =
      std::clamp(state_.library.selected + delta, 0,
                 static_cast<int>(state_.library.visible.size()) - 1);
}

void Controller::moveSelectionTo(bool last) {
  if (state_.page != Page::Library)
    return;
  state_.library.selected =
      last && !state_.library.visible.empty()
          ? static_cast<int>(state_.library.visible.size()) - 1
          : 0;
}

void Controller::removeSelectedFromQueue() {
  // The play queue is deliberately not a browsable collection, so this acts on
  // the CURRENT SONG when the transport owns the keyboard, and on nothing
  // otherwise. It can never delete a row the user is not looking at.
  if (state_.page != Page::Library || state_.focus != FocusArea::PlayerBar)
    return;
  const Song *song = selectedQueueSong();
  if (song == nullptr || !song->queue_id)
    return;
  reportResult(backend_.remove(*song->queue_id), "Removed from the queue");
  refreshQueue();
}

void Controller::movePlaylist(int delta) {
  if (state_.library.playlists.empty())
    return;
  const int size = static_cast<int>(state_.library.playlists.size());
  selectPlaylist((state_.library.current + delta + size) % size);
}

void Controller::moveQueueItem(int delta) {
  const Song *song = selectedQueueSong();
  if (song == nullptr || !song->queue_id || state_.queue.size() < 2U)
    return;
  const int target = std::clamp(state_.selected_queue + delta, 0,
                                static_cast<int>(state_.queue.size()) - 1);
  if (target == state_.selected_queue)
    return;
  const unsigned id = *song->queue_id;
  if (backend_.move(id, static_cast<unsigned>(target))) {
    state_.selected_queue = target;
    refreshQueue();
  } else {
    reportResult(false);
  }
}

bool Controller::collectionExists(const PlaybackCollection &collection) const {
  switch (collection.kind) {
  case PlaybackCollection::Kind::None:
    return false;
  case PlaybackCollection::Kind::Library:
    // The media database is always there; it is MPD's own view of the files.
    return true;
  case PlaybackCollection::Kind::History:
    return history_.epoch() == session_.history_epoch;
  case PlaybackCollection::Kind::Playlist:
    break;
  }
  if (collection.name.empty())
    return false;
  // Every listed name is a real saved playlist -- the first one included. A run
  // owned by the first playlist must keep its marker after a refresh.
  for (std::size_t index = 0; index < state_.library.playlists.size();
       ++index) {
    if (state_.library.playlists[index] == collection.name)
      return true;
  }
  return false;
}

int Controller::occurrenceOf(const std::vector<Song> &tracks,
                             const Song &target) const {
  return occurrenceIndexIn(tracks, target);
}

int Controller::playingRow(const PlaybackCollection &collection,
                           const std::vector<Song> &tracks) const {
  if (!session_.valid())
    return -1;
  const int at = std::clamp(session_.occurrence, 0,
                            static_cast<int>(session_.sequence.size()) - 1);
  const Song &playing = session_.sequence[static_cast<std::size_t>(at)];
  return playingRowIn(collection, session_.context, tracks, playing,
                      session_.context.kind ==
                          PlaybackCollection::Kind::History);
}

void Controller::syncPlaybackSession() {
  const PlayerState &player = state_.player;
  const bool has_song = player.current_song.has_value() &&
                        player.current_song_id >= 0 &&
                        !player.current_song->uri.empty();
  if (!has_song) {
    observed_uri_.clear();
    observed_occurrence_ = 0;
    observed_song_id_ = -1;
    if (player.state == PlaybackState::Stopped)
      session_ = PlaybackSession{};
    return;
  }

  const std::string &uri = player.current_song->uri;
  const int song_id = player.current_song_id;
  if (uri == observed_uri_ && song_id == observed_song_id_)
    return; // same occurrence; pause, seek and volume never move the marker
  observed_uri_ = uri;
  observed_song_id_ = song_id;

  if (!session_.valid()) {
    observed_occurrence_ = 0;
    return; // unknown/external: keep showing no marker anywhere
  }

  const int index = occurrenceOf(session_.sequence, *player.current_song);
  if (index < 0) {
    // Playback left the collection the run came from -- an external client, a
    // deleted playlist, a queue rebuild by someone else. Give up the context
    // rather than leaving a stale marker in a list that is not playing.
    session_ = PlaybackSession{};
    observed_occurrence_ = 0;
    return;
  }
  session_.occurrence = index;
  session_.uri = uri;
  session_.uri_occurrence = player.current_song->playback_occurrence > 0
                                ? player.current_song->playback_occurrence
                                : 1;
  observed_occurrence_ = session_.uri_occurrence;
}

void Controller::stepContext(int delta) {
  if (!session_.valid()) {
    // No context to follow: fall back to MPD's own neighbours rather than
    // inventing an order from a collection the user is merely browsing.
    const bool moved = delta > 0 ? backend_.next() : backend_.previous();
    reportResult(moved);
    refreshPlayer();
    return;
  }
  const int count = static_cast<int>(session_.sequence.size());
  if (count == 0)
    return;

  // SHUFFLE: MPD's random engine owns the order for this session, and this
  // branch is the whole fix. Computing `occurrence + delta` and playing that
  // queue POSITION -- which is what the sequential path below does -- bypasses
  // random completely: an explicit position is an explicit position, whatever
  // the `random` flag says. That is why Shuffle highlighted correctly and the
  // music still walked A, B, C, D.
  //
  // Handing the step to MPD instead means Next, Previous and the automatic
  // advance at end of track all come from ONE order (the queue, which this run
  // built from its snapshot): random picks a not-yet-played entry, and
  // `previous` returns along the history that was actually played. The
  // occurrence and the marker are re-derived from the observation, never
  // guessed here.
  if (state_.player.random) {
    const bool moved = delta > 0 ? backend_.next() : backend_.previous();
    reportResult(moved);
    refreshPlayer();
    return;
  }

  // SEQUENTIAL: the queue IS the snapshot in snapshot order, so stepping one
  // position is exactly "the next occurrence in the context".
  // Wrapping at the ends keeps a playlist usable the way a player is expected
  // to behave, and never leaves the context.
  const int from = std::clamp(session_.occurrence, 0, count - 1);
  const int to = ((from + delta) % count + count) % count;
  if (!backend_.playQueuePosition(static_cast<unsigned>(to))) {
    reportResult(false);
    return;
  }
  session_.occurrence = to;
  session_.uri = session_.sequence[static_cast<std::size_t>(to)].uri;
  session_.uri_occurrence =
      session_.sequence[static_cast<std::size_t>(to)].playback_occurrence;
  session_.history_record_id =
      session_.sequence[static_cast<std::size_t>(to)].history_record_id;
  observed_uri_ = session_.uri;
  observed_song_id_ = -1; // force the next observation to re-resolve it
  refreshPlayer();
}


int Controller::removeHistoryRecords(const std::vector<long long> &ids) {
  int removed = 0;
  for (const long long id : ids) {
    if (history_.removeRecord(id))
      ++removed;
  }
  if (removed == 0)
    return 0;
  // Persist immediately: a manual deletion is a deliberate edit, not a
  // transient view state, so it must survive a restart.
  std::string error;
  if (!history_.save(&error))
    return -1;
  // Deliberately NOT touching `session_`: a run that is already under way keeps
  // its snapshot and continues, even when the record it started from is gone.
  return removed;
}

void Controller::playTrack(const Song &song,
                          const std::vector<Song> &tracks,
                          PlaybackCollection context) {
  if (tracks.empty() || !context.valid()) {
    // Nothing to snapshot: play the single song, but claim no context that
    // could not describe a sequence.
    const int id = backend_.add(song.uri);
    if (id < 0) {
      reportResult(false);
      return;
    }
    reportResult(backend_.playId(static_cast<unsigned>(id)));
    session_ = PlaybackSession{};
    refreshQueue();
    refreshPlayer();
    return;
  }
  int index = occurrenceOf(tracks, song);
  if (index < 0)
    index = 0;
  playTrackAt(tracks, index, std::move(context));
}

void Controller::playTrackAt(const std::vector<Song> &tracks, int index,
                             PlaybackCollection context) {
  if (tracks.empty() || !context.valid() || !backend_.connected())
    return;
  const int start = std::clamp(index, 0, static_cast<int>(tracks.size()) - 1);

  // A fresh snapshot, by design: the run is a COPY, so editing the collection
  // -- or a `default` re-sync -- cannot rewrite what is already playing.
  PlaybackSession session;
  session.context = std::move(context);
  session.sequence = tracks;
  assignOccurrenceCounters(session.sequence);
  session.occurrence = start;
  const Song &row = session.sequence[static_cast<std::size_t>(start)];
  session.uri = row.uri;
  session.uri_occurrence = row.playback_occurrence;
  session.history_record_id = row.history_record_id;
  session.history_epoch =
      session.context.kind == PlaybackCollection::Kind::History
          ? history_.epoch()
          : 0;

  // MPD's queue is the implementation detail that makes Previous, Next and
  // automatic progression follow the CONTEXT: it holds exactly this snapshot,
  // in this order, and nothing else.
  if (backend_.playSequence(session.sequence, start) < 0) {
    reportResult(false);
    return;
  }
  session.owns_queue = true;
  session_ = std::move(session);
  observed_uri_ = session_.uri;
  observed_occurrence_ = session_.uri_occurrence;
  observed_song_id_ = -1; // re-resolve from the fresh player sample
  refreshQueue();
  refreshPlayer();
}


void Controller::reportResult(bool success, std::string_view success_message) {
  if (!success) {
    state_.mpd_connected = backend_.connected();
    state_.error = backend_.lastError();
    if (state_.error->empty())
      state_.error = "MPD command failed";
    return;
  }
  state_.mpd_connected = true;
  state_.error.reset();
  if (!success_message.empty())
    toast(std::string(success_message));
}

void Controller::toast(std::string message) {
  state_.toast = std::move(message);
  state_.toast_at = std::chrono::steady_clock::now();
}

void Controller::syncSettingsState() {
  state_.settings.start_page = config_.start_page;
  state_.settings.seek_step = config_.seek_step;
  state_.settings.volume_step = config_.volume_step;
  state_.settings.library_path = config_.library_path;
  state_.settings.mpd_host = config_.mpd_host;
  state_.settings.mpd_port = config_.mpd_port;
  state_.settings.auto_reconnect = config_.auto_reconnect;
  // The EFFECTIVE timeout, so the running contract (never 0) is observable.
  state_.settings.mpd_timeout_ms = connection_.timeout_ms;
  state_.settings.theme_name = config_.theme_name;
  state_.settings.visualizer_refresh_hz = config_.visualizer_refresh_hz;
  state_.settings.visualizer_bar_density = config_.visualizer_bar_density;
  state_.settings.visualizer_fifo = config_.visualizer_fifo;
  state_.settings.visualizer_sample_rate = config_.visualizer_sample_rate;
  state_.settings.visualizer_channels = config_.visualizer_channels;
}

} // namespace termusic
