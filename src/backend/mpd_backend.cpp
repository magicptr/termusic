#include "backend/mpd_backend.hpp"

#include <mpd/client.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <sys/types.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string_view>
#include <utility>

namespace termusic {
namespace {

using StatusPtr = std::unique_ptr<mpd_status, decltype(&mpd_status_free)>;
using SongPtr = std::unique_ptr<mpd_song, decltype(&mpd_song_free)>;
using EntityPtr = std::unique_ptr<mpd_entity, decltype(&mpd_entity_free)>;
using PlaylistPtr = std::unique_ptr<mpd_playlist, decltype(&mpd_playlist_free)>;

std::string tag(const mpd_song *song, mpd_tag_type type) {
  const char *value = mpd_song_get_tag(song, type, 0);
  return value == nullptr ? std::string{} : std::string(value);
}

/// "FLAC 16bit/44.1kHz" from the container's own audio format plus the file
/// extension. Only what MPD reports is used; an unknown format stays empty so
/// the immersive metadata block can hide the row instead of inventing one.
std::string describeFormat(const mpd_song *song, std::string_view uri) {
  std::string codec;
  const auto dot = uri.find_last_of('.');
  if (dot != std::string_view::npos && dot + 1 < uri.size()) {
    for (const char c : uri.substr(dot + 1))
      codec.push_back(
          static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  const mpd_audio_format *format = mpd_song_get_audio_format(song);
  if (format == nullptr || format->sample_rate <= 0)
    return codec;
  const double khz = static_cast<double>(format->sample_rate) / 1000.0;
  char rate[32];
  std::snprintf(rate, sizeof(rate), "%.1fkHz", khz);
  const std::string detail = std::to_string(format->bits) + "bit/" + rate;
  return codec.empty() ? detail : codec + " " + detail;
}

Song convertSong(const mpd_song *source) {
  Song song;
  if (const char *uri = mpd_song_get_uri(source); uri != nullptr) {
    song.uri = uri;
  }
  song.title = tag(source, MPD_TAG_TITLE);
  song.artist = tag(source, MPD_TAG_ARTIST);
  song.album = tag(source, MPD_TAG_ALBUM);
  song.genre = tag(source, MPD_TAG_GENRE);
  // MPD's Date tag may carry a full date; the year is what the UI shows.
  const std::string date = tag(source, MPD_TAG_DATE);
  song.year = date.size() >= 4 ? date.substr(0, 4) : date;
  song.format = describeFormat(source, song.uri);
  song.duration_seconds =
      static_cast<double>(mpd_song_get_duration_ms(source)) / 1000.0;

  const unsigned id = mpd_song_get_id(source);
  if (id != UINT_MAX)
    song.queue_id = id;
  const unsigned position = mpd_song_get_pos(source);
  if (position != UINT_MAX)
    song.queue_position = position;
  return song;
}

/// One song read from a pair stream: the converted track plus the attributes
/// libmpdclient's `mpd_song` object does not model.
struct ParsedSong {
  Song song;
  /// MPD's `size` attribute in bytes. 0 means the response did not carry one.
  double size_bytes = 0.0;
};

/// Returns a pair to the connection it came from. mpd_return_pair() needs the
/// connection, so the deleter carries it rather than being a bare function.
struct PairDeleter {
  mpd_connection *connection = nullptr;
  void operator()(mpd_pair *pair) const {
    if (pair != nullptr)
      mpd_return_pair(connection, pair);
  }
};

using PairPtr = std::unique_ptr<mpd_pair, PairDeleter>;

PairPtr receivePair(mpd_connection *connection) {
  return PairPtr(mpd_recv_pair(connection), PairDeleter{connection});
}

/// Reads the `size` attribute of every file under one directory. Files land in
/// `sizes`; subdirectories land in `directories` for the caller to descend
/// into, because `listfiles` reports one level at a time.
///
/// A separate command is required, and not by choice: MPD's database response
/// (`listallinfo`) reports tags only -- it does not include `size` at all,
/// even though the protocol documents the attribute -- while `listfiles`
/// reports the filesystem, size included, and omits the tags. The two are
/// therefore complementary, and the Library table needs both.
///
/// `listfiles` names both files and subdirectories by their BASENAME, so the
/// directory being read is prepended here: the map is keyed by MPD URI, which
/// is what the database and the UI agree on.
bool readDirectorySizes(mpd_connection *connection, std::string_view path,
                        std::map<std::string, double> &sizes,
                        std::vector<std::string> &directories) {
  const std::string copy(path);
  const bool sent =
      path.empty() ? mpd_send_command(connection, "listfiles", nullptr)
                   : mpd_send_command(connection, "listfiles", copy.c_str(),
                                      nullptr);
  if (!sent)
    return false;

  // `path` is "" at the music root, where a basename is already a URI.
  const auto uri_of = [&](const std::string &basename) {
    return copy.empty() ? basename : copy + "/" + basename;
  };

  std::string current_file;
  for (;;) {
    PairPtr pair = receivePair(connection);
    if (!pair)
      return mpd_connection_get_error(connection) == MPD_ERROR_SUCCESS;
    if (pair->name == nullptr || pair->value == nullptr)
      continue;
    const std::string_view name(pair->name);
    if (name == "file") {
      current_file = pair->value;
      continue;
    }
    if (name == "directory") {
      current_file.clear();
      directories.push_back(uri_of(pair->value));
      continue;
    }
    if (name != "size")
      continue;
    // A file whose size MPD could not stat gets no size attribute at all, and
    // a size of 0 means "unknown" as far as the UI is concerned: both are left
    // out, which is what makes the column print "--".
    if (current_file.empty())
      continue;
    char *end = nullptr;
    const double parsed = std::strtod(pair->value, &end);
    if (end != pair->value && parsed > 0.0)
      sizes[uri_of(current_file)] = parsed;
  }
}

/// Walks the whole tree, one `listfiles` round trip per directory, and returns
/// URI -> byte size for every file MPD can measure.
bool readAllFileSizes(mpd_connection *connection,
                      std::map<std::string, double> &sizes) {
  std::vector<std::string> pending;
  pending.emplace_back();
  // The queue only ever grows by directories that actually exist, so a
  // circular mount cannot make this loop forever.
  for (std::size_t index = 0; index < pending.size(); ++index) {
    std::vector<std::string> directories;
    if (!readDirectorySizes(connection, pending[index], sizes, directories))
      return false;
    for (std::string &directory : directories)
      pending.push_back(std::move(directory));
  }
  return true;
}

/// True when this pair opens a new song. Only a `file` pair may be handed to
/// mpd_song_begin(), and any other pair is either fed to the song or dropped.
bool feedPair(mpd_song *song, const mpd_pair *pair, double &size_bytes) {
  const std::string_view name(pair->name == nullptr ? "" : pair->name);
  if (name == "file")
    return false;
  if (name == "size") {
    // libmpdclient 2.26 has no mpd_song_get_size(): mpd_song_feed() accepts
    // and silently ignores this attribute. Read it here, before the feed, so
    // the Library table can show a real file size.
    if (pair->value != nullptr) {
      char *end = nullptr;
      const double parsed = std::strtod(pair->value, &end);
      if (end != pair->value && parsed > 0.0)
        size_bytes = parsed;
    }
    return true;
  }
  mpd_song_feed(song, pair);
  return true;
}

/// Reads a whole song list at the PAIR level.
///
/// Every command that lists songs (`listallinfo`, `lsinfo`,
/// `listplaylistinfo`, queue listings) returns the same `file ...` element
/// shape, so one reader serves them all and the byte size is captured in the
/// same single round trip. Returns false on a protocol or connection error,
/// which the caller reports through finish() exactly as before.
bool receiveSongs(mpd_connection *connection, std::vector<ParsedSong> &out) {
  SongPtr song(nullptr, mpd_song_free);
  double size_bytes = 0.0;
  const auto flush = [&] {
    if (!song)
      return;
    ParsedSong parsed;
    parsed.song = convertSong(song.get());
    parsed.size_bytes = size_bytes;
    out.push_back(std::move(parsed));
    song.reset();
    size_bytes = 0.0;
  };

  for (;;) {
    PairPtr pair = receivePair(connection);
    if (!pair)
      break;
    if (pair->name == nullptr)
      continue;
    if (std::string_view(pair->name) == "file") {
      flush();
      song.reset(mpd_song_begin(pair.get()));
      size_bytes = 0.0;
      continue;
    }
    if (song)
      feedPair(song.get(), pair.get(), size_bytes);
  }
  // The LAST song of a response is never followed by another `file` pair, so it
  // is still held here. Without this flush every listing silently dropped its
  // final track -- the library count came out one short.
  flush();
  // A null pair is either the end of the response or an error; the caller
  // distinguishes them by asking the connection.
  return mpd_connection_get_error(connection) == MPD_ERROR_SUCCESS;
}

BackendEvent convertEvents(mpd_idle changed) {
  BackendEvent result = BackendEvent::None;
  if ((changed & MPD_IDLE_PLAYER) != 0)
    result = result | BackendEvent::Player;
  if ((changed & MPD_IDLE_QUEUE) != 0)
    result = result | BackendEvent::Queue;
  if ((changed & MPD_IDLE_DATABASE) != 0)
    result = result | BackendEvent::Database;
  if ((changed & MPD_IDLE_MIXER) != 0)
    result = result | BackendEvent::Mixer;
  if ((changed & MPD_IDLE_OPTIONS) != 0)
    result = result | BackendEvent::Options;
  if ((changed & MPD_IDLE_UPDATE) != 0)
    result = result | BackendEvent::Update;
  return result;
}

} // namespace

void MpdBackend::ConnectionDeleter::operator()(
    mpd_connection *connection) const {
  if (connection != nullptr)
    mpd_connection_free(connection);
}

MpdBackend::MpdBackend() = default;
MpdBackend::~MpdBackend() { disconnect(); }

MpdBackend::ConnectionPtr MpdBackend::makeConnection() const {
  const char *host = options_.host.empty() ? nullptr : options_.host.c_str();
  ConnectionPtr connection(
      mpd_connection_new(host, options_.port, options_.timeout_ms));
  if (connection == nullptr ||
      mpd_connection_get_error(connection.get()) != MPD_ERROR_SUCCESS) {
    return connection;
  }
  if (!options_.password.empty() &&
      !mpd_run_password(connection.get(), options_.password.c_str())) {
    return connection;
  }
  return connection;
}

bool MpdBackend::reachable(const MpdConnectionOptions &options,
                           int budget_ms) const {
  // A unix socket (or the library's own default host) connects locally and
  // immediately: there is nothing to bound, so no probe is needed.
  if (options.host.empty() || options.host.front() == '/')
    return true;

  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *addresses = nullptr;
  const std::string port = std::to_string(options.port);
  if (::getaddrinfo(options.host.c_str(), port.c_str(), &hints, &addresses) != 0)
    return false;
  bool opened = false;
  for (struct addrinfo *address = addresses; address != nullptr && !opened;
       address = address->ai_next) {
    const int socket_fd =
        ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socket_fd < 0)
      continue;
    const int flags = ::fcntl(socket_fd, F_GETFL, 0);
    (void)::fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
    const int result =
        ::connect(socket_fd, address->ai_addr, address->ai_addrlen);
    if (result == 0) {
      opened = true;
    } else if (errno == EINPROGRESS) {
      struct pollfd descriptor {};
      descriptor.fd = socket_fd;
      descriptor.events = POLLOUT;
      const int ready = ::poll(&descriptor, 1, std::max(1, budget_ms));
      if (ready > 0) {
        int error = 0;
        socklen_t length = sizeof(error);
        if (::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
            error == 0)
          opened = true;
      }
    }
    ::close(socket_fd);
  }
  ::freeaddrinfo(addresses);
  return opened;
}

bool MpdBackend::connect(const MpdConnectionOptions &options) {
  disconnect();
  options_ = options;
  command_ = makeConnection();
  if (command_ == nullptr ||
      mpd_connection_get_error(command_.get()) != MPD_ERROR_SUCCESS) {
    recordConnectionError(command_.get(), "Unable to connect to MPD");
    command_.reset();
    return false;
  }

  events_ = makeConnection();
  if (events_ == nullptr ||
      mpd_connection_get_error(events_.get()) != MPD_ERROR_SUCCESS) {
    recordConnectionError(events_.get(), "Unable to open MPD event connection");
    events_.reset();
    command_.reset();
    return false;
  }
  {
    std::scoped_lock lock(error_mutex_);
    last_error_.clear();
  }
  events_healthy_.store(true);
  return true;
}

void MpdBackend::disconnect() {
  stopEventLoop();
  events_healthy_.store(false);
  events_.reset();
  command_.reset();
  // A different server may expose a different library under the same URIs.
  size_by_uri_.clear();
}

bool MpdBackend::connected() const {
  return command_ != nullptr && events_healthy_.load() &&
         mpd_connection_get_error(command_.get()) == MPD_ERROR_SUCCESS &&
         events_ != nullptr;
}

std::string MpdBackend::lastError() const {
  std::scoped_lock lock(error_mutex_);
  return last_error_;
}

bool MpdBackend::commandReady() {
  if (command_ == nullptr) {
    std::scoped_lock lock(error_mutex_);
    last_error_ = "MPD is disconnected";
    return false;
  }
  if (mpd_connection_get_error(command_.get()) == MPD_ERROR_SUCCESS)
    return true;
  recordConnectionError(command_.get(), "MPD command failed");
  return mpd_connection_clear_error(command_.get());
}

bool MpdBackend::finish(bool result) {
  if (result)
    return true;
  recordConnectionError(command_.get(), "MPD command failed");
  if (command_ != nullptr)
    (void)mpd_connection_clear_error(command_.get());
  return false;
}

void MpdBackend::attachCachedSize(Song &song) const {
  if (song.file_size_bytes > 0.0 || song.uri.empty())
    return;
  const auto found = size_by_uri_.find(song.uri);
  if (found != size_by_uri_.end())
    song.file_size_bytes = found->second;
}

namespace {
/// A concise, actionable reason a connection attempt failed.
///
/// The raw libmpdclient string is used only when nothing more specific is
/// known: "Connection refused" or "Authentication failed" tells a user what to
/// do, while an internal errno dump does not. This is the MPD-boundary's job --
/// nothing above it should have to know MPD error codes.
std::string describeConnectionError(mpd_connection *connection,
                                    std::string_view fallback) {
  if (connection == nullptr)
    return std::string(fallback);
  switch (mpd_connection_get_error(connection)) {
  case MPD_ERROR_TIMEOUT:
    return "Timed out";
  case MPD_ERROR_RESOLVER:
    return "Host not found";
  case MPD_ERROR_CLOSED:
    return "Connection closed by the server";
  case MPD_ERROR_SYSTEM:
    switch (mpd_connection_get_system_error(connection)) {
    case ECONNREFUSED:
      return "Connection refused";
    case ENETUNREACH:
      return "Network unreachable";
    case EHOSTUNREACH:
      return "Host unreachable";
    case ETIMEDOUT:
      return "Timed out";
    case EACCES:
      return "Permission denied";
    default:
      break;
    }
    break;
  case MPD_ERROR_SERVER:
    switch (mpd_connection_get_server_error(connection)) {
    case MPD_SERVER_ERROR_PASSWORD:
      return "Authentication failed";
    case MPD_SERVER_ERROR_PERMISSION:
      return "Permission denied";
    case MPD_SERVER_ERROR_NO_EXIST:
      return "Not found on the server";
    default:
      break;
    }
    break;
  default:
    break;
  }
  if (const char *detail = mpd_connection_get_error_message(connection);
      detail != nullptr && *detail != '\0') {
    return detail;
  }
  return std::string(fallback);
}
} // namespace

void MpdBackend::recordConnectionError(mpd_connection *connection,
                                       std::string_view fallback) {
  std::string message = describeConnectionError(connection, fallback);
  std::scoped_lock lock(error_mutex_);
  last_error_ = std::move(message);
}

PlayerState MpdBackend::fetchPlayerState() {
  PlayerState player;
  player.sampled_at = std::chrono::steady_clock::now();
  if (!commandReady())
    return player;

  StatusPtr status(mpd_run_status(command_.get()), mpd_status_free);
  if (!status) {
    finish(false);
    return player;
  }
  switch (mpd_status_get_state(status.get())) {
  case MPD_STATE_PLAY:
    player.state = PlaybackState::Playing;
    break;
  case MPD_STATE_PAUSE:
    player.state = PlaybackState::Paused;
    break;
  default:
    player.state = PlaybackState::Stopped;
    break;
  }
  player.volume = mpd_status_get_volume(status.get());
  player.repeat = mpd_status_get_repeat(status.get());
  player.random = mpd_status_get_random(status.get());
  player.elapsed_seconds =
      static_cast<double>(mpd_status_get_elapsed_ms(status.get())) / 1000.0;
  player.duration_seconds =
      static_cast<double>(mpd_status_get_total_time(status.get()));
  const int song_id = mpd_status_get_song_id(status.get());
  if (song_id >= 0)
    player.current_song_id = song_id;

  SongPtr current(mpd_run_current_song(command_.get()), mpd_song_free);
  if (current) {
    player.current_song = convertSong(current.get());
    if (player.current_song->duration_seconds > 0.0) {
      player.duration_seconds = player.current_song->duration_seconds;
    }
    attachCachedSize(*player.current_song);
  } else if (mpd_connection_get_error(command_.get()) != MPD_ERROR_SUCCESS) {
    finish(false);
  }
  return player;
}

std::vector<Song> MpdBackend::fetchQueue() {
  std::vector<Song> result;
  if (!commandReady() || !mpd_send_list_queue_meta(command_.get())) {
    finish(false);
    return result;
  }
  std::vector<ParsedSong> parsed;
  const bool complete = receiveSongs(command_.get(), parsed);
  finish(mpd_response_finish(command_.get()) && complete);
  result.reserve(parsed.size());
  for (ParsedSong &entry : parsed)
    result.push_back(std::move(entry.song));
  return result;
}

std::vector<Song> MpdBackend::fetchAllSongs() {
  std::vector<Song> result;
  if (!commandReady() || !mpd_send_list_all_meta(command_.get(), nullptr)) {
    finish(false);
    return result;
  }
  std::vector<ParsedSong> parsed;
  const bool complete = receiveSongs(command_.get(), parsed);
  bool ok = finish(mpd_response_finish(command_.get())) && complete;
  if (!ok)
    return result;

  // The database carries the tags; `listfiles` carries the byte sizes, and
  // MPD sends them in no other response. Sizes are cached so the directory
  // browser and the current-track display -- whose responses have no `size`
  // either -- can still show one for a file the database already knows.
  std::map<std::string, double> sizes;
  const bool sizes_ok = readAllFileSizes(command_.get(), sizes);
  if (sizes_ok)
    ok = finish(mpd_response_finish(command_.get()));
  if (ok && !sizes.empty())
    size_by_uri_ = std::move(sizes);

  result.reserve(parsed.size());
  for (ParsedSong &entry : parsed) {
    if (entry.size_bytes > 0.0)
      entry.song.file_size_bytes = entry.size_bytes;
    attachCachedSize(entry.song);
    result.push_back(std::move(entry.song));
  }
  return result;
}

std::vector<Song> MpdBackend::search(std::string_view query) {
  std::vector<Song> result;
  if (!commandReady())
    return result;
  const std::string query_copy(query);
  if (!mpd_search_db_songs(command_.get(), false) ||
      !mpd_search_add_any_tag_constraint(command_.get(), MPD_OPERATOR_DEFAULT,
                                         query_copy.c_str()) ||
      !mpd_search_commit(command_.get())) {
    finish(false);
    return result;
  }
  std::vector<ParsedSong> parsed;
  const bool complete = receiveSongs(command_.get(), parsed);
  finish(mpd_response_finish(command_.get()) && complete);
  result.reserve(parsed.size());
  for (ParsedSong &entry : parsed) {
    entry.song.file_size_bytes = entry.size_bytes;
    attachCachedSize(entry.song);
    result.push_back(std::move(entry.song));
  }
  return result;
}

std::vector<std::string> MpdBackend::listPlaylists() {
  std::vector<std::string> result;
  if (!commandReady() || !mpd_send_list_playlists(command_.get())) {
    finish(false);
    return result;
  }
  while (mpd_playlist *raw = mpd_recv_playlist(command_.get())) {
    PlaylistPtr playlist(raw, mpd_playlist_free);
    if (const char *name = mpd_playlist_get_path(playlist.get());
        name != nullptr) {
      result.emplace_back(name);
    }
  }
  finish(mpd_response_finish(command_.get()));
  return result;
}

std::vector<Song> MpdBackend::listPlaylist(std::string_view name) {
  std::vector<Song> result;
  if (!commandReady())
    return result;
  const std::string copy(name);
  if (!mpd_send_list_playlist_meta(command_.get(), copy.c_str())) {
    finish(false);
    return result;
  }
  while (mpd_entity *raw = mpd_recv_entity(command_.get())) {
    EntityPtr entity(raw, mpd_entity_free);
    if (mpd_entity_get_type(entity.get()) == MPD_ENTITY_TYPE_SONG) {
      result.push_back(convertSong(mpd_entity_get_song(entity.get())));
    }
  }
  finish(mpd_response_finish(command_.get()));
  return result;
}

bool MpdBackend::playQueuePosition(unsigned position) {
  if (!commandReady())
    return false;
  return finish(mpd_run_play_pos(command_.get(), position));
}

bool MpdBackend::play() {
  return commandReady() && finish(mpd_run_play(command_.get()));
}
bool MpdBackend::playId(unsigned id) {
  return commandReady() && finish(mpd_run_play_id(command_.get(), id));
}
bool MpdBackend::pause(bool paused) {
  return commandReady() && finish(mpd_run_pause(command_.get(), paused));
}
bool MpdBackend::stop() {
  return commandReady() && finish(mpd_run_stop(command_.get()));
}
bool MpdBackend::next() {
  return commandReady() && finish(mpd_run_next(command_.get()));
}
bool MpdBackend::previous() {
  return commandReady() && finish(mpd_run_previous(command_.get()));
}
bool MpdBackend::seekAbsolute(double seconds) {
  const float value = static_cast<float>(std::max(0.0, seconds));
  return commandReady() &&
         finish(mpd_run_seek_current(command_.get(), value, false));
}
bool MpdBackend::seekRelative(double seconds) {
  return commandReady() &&
         finish(mpd_run_seek_current(command_.get(),
                                     static_cast<float>(seconds), true));
}
bool MpdBackend::setVolume(unsigned volume) {
  return commandReady() &&
         finish(mpd_run_set_volume(command_.get(), std::min(volume, 100U)));
}
bool MpdBackend::setRepeat(bool enabled) {
  return commandReady() && finish(mpd_run_repeat(command_.get(), enabled));
}
bool MpdBackend::setRandom(bool enabled) {
  return commandReady() && finish(mpd_run_random(command_.get(), enabled));
}

int MpdBackend::add(std::string_view uri) {
  if (!commandReady())
    return -1;
  const std::string copy(uri);
  const int id = mpd_run_add_id(command_.get(), copy.c_str());
  if (id < 0)
    finish(false);
  return id;
}
bool MpdBackend::remove(unsigned queue_id) {
  return commandReady() && finish(mpd_run_delete_id(command_.get(), queue_id));
}
bool MpdBackend::move(unsigned queue_id, unsigned new_position) {
  return commandReady() &&
         finish(mpd_run_move_id(command_.get(), queue_id, new_position));
}
bool MpdBackend::clearQueue() {
  return commandReady() && finish(mpd_run_clear(command_.get()));
}

int MpdBackend::playSequence(const std::vector<Song> &songs, int start_index) {
  if (!commandReady() || songs.empty())
    return -1;
  const int start = std::clamp(start_index, 0,
                               static_cast<int>(songs.size()) - 1);
  if (!mpd_command_list_begin(command_.get(), false))
    return finish(false), -1;
  if (!mpd_send_clear(command_.get()))
    return finish(false), -1;
  for (const Song &song : songs) {
    if (song.uri.empty())
      continue;
    if (!mpd_send_add(command_.get(), song.uri.c_str()))
      return finish(false), -1;
  }
  if (!mpd_command_list_end(command_.get()))
    return finish(false), -1;
  if (!mpd_response_finish(command_.get()))
    return finish(false), -1;
  // Start it as a separate command: the position is only meaningful once the
  // adds above have been applied. `mpd_run_play_pos` starts the song, so the
  // caller learns whether playback began; the identity of what is playing is
  // read back from the player state like every other observation.
  if (!mpd_run_play_pos(command_.get(), static_cast<unsigned>(start)))
    return finish(false), -1;
  return start;
}

bool MpdBackend::createPlaylist(std::string_view name) {
  if (!commandReady() || name.empty())
    return false;
  const std::string copy(name);
  if (!finish(mpd_run_save(command_.get(), copy.c_str())))
    return false;
  if (finish(mpd_run_playlist_clear(command_.get(), copy.c_str())))
    return true;
  // save() has already created the playlist. Roll it back if the clear failed
  // so callers never observe a failed operation that left a full queue copy.
  if (commandReady())
    (void)finish(mpd_run_rm(command_.get(), copy.c_str()));
  return false;
}

bool MpdBackend::syncPlaylist(std::string_view name,
                              const std::vector<Song> &songs) {
  if (!commandReady() || name.empty())
    return false;
  const std::string playlist(name);
  // A saved playlist cannot be cleared before it exists, and `playlistadd`
  // creates one on demand -- so existence decides whether a clear is issued.
  // This is a read of the playlist index only; the contents are the caller's
  // business (change detection happens there).
  bool exists = false;
  for (const std::string &candidate : listPlaylists()) {
    if (candidate == name) {
      exists = true;
      break;
    }
  }
  if (!commandReady())
    return false;

  // The clear and every add travel as ONE command list: MPD applies them in
  // order in a single round trip, so a connection failure can no longer leave
  // the playlist emptied but not refilled by a later round trip.
  if (!mpd_command_list_begin(command_.get(), false))
    return finish(false);
  if (exists &&
      !mpd_send_playlist_clear(command_.get(), playlist.c_str()))
    return finish(false);
  for (const Song &song : songs) {
    if (song.uri.empty())
      continue;
    if (!mpd_send_playlist_add(command_.get(), playlist.c_str(),
                               song.uri.c_str()))
      return finish(false);
  }
  if (!mpd_command_list_end(command_.get()))
    return finish(false);
  return finish(mpd_response_finish(command_.get()));
}

bool MpdBackend::renamePlaylist(std::string_view from, std::string_view to) {
  const std::string old_name(from);
  const std::string new_name(to);
  return commandReady() && !old_name.empty() && !new_name.empty() &&
         finish(mpd_run_rename(command_.get(), old_name.c_str(),
                               new_name.c_str()));
}
bool MpdBackend::deletePlaylist(std::string_view name) {
  const std::string copy(name);
  return commandReady() && !copy.empty() &&
         finish(mpd_run_rm(command_.get(), copy.c_str()));
}
bool MpdBackend::addToPlaylist(std::string_view name, std::string_view uri) {
  const std::string playlist(name);
  const std::string path(uri);
  return commandReady() && !playlist.empty() && !path.empty() &&
         finish(mpd_run_playlist_add(command_.get(), playlist.c_str(),
                                     path.c_str()));
}
bool MpdBackend::removeFromPlaylist(std::string_view name, unsigned position) {
  const std::string copy(name);
  return commandReady() && !copy.empty() &&
         finish(
             mpd_run_playlist_delete(command_.get(), copy.c_str(), position));
}
bool MpdBackend::moveInPlaylist(std::string_view name, unsigned from,
                                unsigned to) {
  const std::string copy(name);
  return commandReady() && !copy.empty() &&
         finish(mpd_run_playlist_move(command_.get(), copy.c_str(), from, to));
}
bool MpdBackend::loadPlaylistToQueue(std::string_view name) {
  const std::string copy(name);
  if (!commandReady() || copy.empty())
    return false;
  if (!finish(mpd_run_clear(command_.get())))
    return false;
  return finish(mpd_run_load(command_.get(), copy.c_str()));
}
bool MpdBackend::updateDatabase() {
  if (!commandReady())
    return false;
  const unsigned id = mpd_run_update(command_.get(), nullptr);
  if (id == 0U)
    return finish(false);
  return true;
}

void MpdBackend::startEventLoop(EventCallback callback) {
  stopEventLoop();
  if (events_ == nullptr)
    return;
  event_thread_ = std::jthread(
      [this, callback = std::move(callback)](std::stop_token stop) mutable {
        eventLoop(stop, std::move(callback));
      });
}

void MpdBackend::requestEventLoopStop() {
  if (event_thread_.joinable())
    event_thread_.request_stop();
}

void MpdBackend::stopEventLoop() {
  requestEventLoopStop();
  if (event_thread_.joinable()) {
    event_thread_.join();
  }
}

void MpdBackend::eventLoop(std::stop_token stop, EventCallback callback) {
  constexpr auto kMask = static_cast<mpd_idle>(
      MPD_IDLE_PLAYER | MPD_IDLE_QUEUE | MPD_IDLE_DATABASE | MPD_IDLE_MIXER |
      MPD_IDLE_OPTIONS | MPD_IDLE_UPDATE);
  if (!mpd_send_idle_mask(events_.get(), kMask)) {
    events_healthy_.store(false);
    recordConnectionError(events_.get(), "Unable to subscribe to MPD events");
    return;
  }
  bool idle_pending = true;

  while (!stop.stop_requested()) {
    pollfd descriptor{mpd_connection_get_fd(events_.get()), POLLIN, 0};
    const int result = ::poll(&descriptor, 1, 250);
    if (result == 0)
      continue;
    if (result < 0 ||
        (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      events_healthy_.store(false);
      recordConnectionError(events_.get(), "MPD event connection closed");
      return;
    }
    if ((descriptor.revents & POLLIN) == 0)
      continue;
    const mpd_idle changed = mpd_recv_idle(events_.get(), false);
    idle_pending = false;
    if (changed == 0 &&
        mpd_connection_get_error(events_.get()) != MPD_ERROR_SUCCESS) {
      events_healthy_.store(false);
      recordConnectionError(events_.get(), "MPD event receive failed");
      return;
    }
    if (changed != 0)
      callback(convertEvents(changed));
    if (!stop.stop_requested()) {
      if (!mpd_send_idle_mask(events_.get(), kMask)) {
        events_healthy_.store(false);
        recordConnectionError(events_.get(), "MPD event subscription failed");
        return;
      }
      idle_pending = true;
    }
  }
  if (idle_pending && events_ != nullptr &&
      mpd_connection_get_error(events_.get()) == MPD_ERROR_SUCCESS) {
    (void)mpd_send_noidle(events_.get());
    (void)mpd_recv_idle(events_.get(), false);
  }
}

} // namespace termusic
