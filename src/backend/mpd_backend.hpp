#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "app/state.hpp"

struct mpd_connection;

namespace termusic {

enum class BackendEvent : std::uint32_t {
  None = 0,
  Player = 1U << 0U,
  Queue = 1U << 1U,
  Database = 1U << 2U,
  Mixer = 1U << 3U,
  Options = 1U << 4U,
  Update = 1U << 5U,
};

constexpr BackendEvent operator|(BackendEvent lhs, BackendEvent rhs) {
  return static_cast<BackendEvent>(static_cast<std::uint32_t>(lhs) |
                                   static_cast<std::uint32_t>(rhs));
}

constexpr bool hasEvent(BackendEvent value, BackendEvent flag) {
  return (static_cast<std::uint32_t>(value) &
          static_cast<std::uint32_t>(flag)) != 0U;
}

struct MpdConnectionOptions {
  std::string host;
  unsigned port = 0;
  /// Socket timeout in milliseconds. NEVER zero: libmpdclient treats 0 as
  /// "wait forever", so a single unresponsive server would block every call
  /// -- including the stop() that quit performs -- and the process could not
  /// exit. Two seconds is far above any healthy round trip and still short
  /// enough that quitting always finishes.
  unsigned timeout_ms = 2000;
  std::string password;
};

class MpdBackend {
public:
  using EventCallback = std::function<void(BackendEvent)>;

  MpdBackend();
  ~MpdBackend();
  MpdBackend(const MpdBackend &) = delete;
  MpdBackend &operator=(const MpdBackend &) = delete;

  bool connect(const MpdConnectionOptions &options = {});
  /// A short, NON-DESTRUCTIVE reachability probe: can a TCP connection to
  /// `options.host:options.port` be opened within `budget_ms`?
  ///
  /// It sends no MPD command and opens no MPD session -- it only decides whether
  /// a real connect() is worth attempting RIGHT NOW, so an unreachable address
  /// cannot freeze the caller for the whole socket timeout. A local socket path
  /// (or an empty host, which means "the library default") needs no probe and
  /// reports reachable.
  bool reachable(const MpdConnectionOptions &options, int budget_ms) const;
  void disconnect();
  bool connected() const;
  std::string lastError() const;

  PlayerState fetchPlayerState();
  std::vector<Song> fetchQueue();
  /// The complete media database, in one `listallinfo` round trip. MPD reports
  /// no `size` attribute there, so one `listfiles` pass follows to learn the
  /// byte size of every file, and both are cached for the views whose own
  /// responses cannot carry a size.
  std::vector<Song> fetchAllSongs();
  std::vector<Song> search(std::string_view query);
  std::vector<std::string> listPlaylists();
  std::vector<Song> listPlaylist(std::string_view name);

  bool play();
  bool playId(unsigned id);
  /// Plays the entry already at `position` in MPD's queue. This is how a
  /// playback run moves inside its own snapshot without rebuilding the queue.
  bool playQueuePosition(unsigned position);
  bool pause(bool paused);
  bool stop();
  bool next();
  bool previous();
  bool seekAbsolute(double seconds);
  bool seekRelative(double seconds);
  bool setVolume(unsigned volume);
  bool setRepeat(bool enabled);
  bool setRandom(bool enabled);

  int add(std::string_view uri);
  /// Replaces MPD's queue with `songs`, in exactly that order, and starts
  /// playing position `start_index`. One command list does the clear and every
  /// add, so the queue is never left holding a half-built sequence -- and the
  /// queue order IS the collection order, which is what makes Previous, Next
  /// and automatic progression follow the playback context.
  ///
  /// Returns the started position, or -1 on failure.
  int playSequence(const std::vector<Song> &songs, int start_index);
  bool remove(unsigned queue_id);
  bool move(unsigned queue_id, unsigned new_position);
  bool clearQueue();

  bool createPlaylist(std::string_view name);
  /// Replaces a saved playlist's contents with `songs`, in library order.
  ///
  /// One centralized operation: the whole replacement travels as a single MPD
  /// command list, so the playlist is never left half-written by a round-trip
  /// failure. The caller is responsible for change detection -- this always
  /// writes. An existing playlist is cleared first; a missing one is created
  /// by the first add.
  bool syncPlaylist(std::string_view name, const std::vector<Song> &songs);
  bool renamePlaylist(std::string_view from, std::string_view to);
  bool deletePlaylist(std::string_view name);
  bool addToPlaylist(std::string_view name, std::string_view uri);
  bool removeFromPlaylist(std::string_view name, unsigned position);
  bool moveInPlaylist(std::string_view name, unsigned from, unsigned to);
  bool loadPlaylistToQueue(std::string_view name);
  bool updateDatabase();

  void startEventLoop(EventCallback callback);
  /// Requests shutdown without waiting for the MPD event thread to finish.
  void requestEventLoopStop();
  void stopEventLoop();

private:
  struct ConnectionDeleter {
    void operator()(mpd_connection *connection) const;
  };
  using ConnectionPtr = std::unique_ptr<mpd_connection, ConnectionDeleter>;

  ConnectionPtr makeConnection() const;
  bool commandReady();
  bool finish(bool result);
  /// Fills `song.file_size_bytes` from the last full song list, for commands
  /// whose MPD response has no `size` attribute (`lsinfo`, `listplaylistinfo`).
  void attachCachedSize(Song &song) const;
  void recordConnectionError(mpd_connection *connection,
                             std::string_view fallback);
  void eventLoop(std::stop_token stop, EventCallback callback);

  MpdConnectionOptions options_;
  ConnectionPtr command_;
  ConnectionPtr events_;
  /// URI -> file size in bytes, learned from the last `listallinfo` snapshot.
  /// Only the command connection and its owning UI thread touch it; the MPD
  /// event thread never reads it, so it needs no lock of its own.
  std::map<std::string, double> size_by_uri_;
  /// Published by the event thread so connected() never concurrently probes
  /// libmpdclient's event connection while that thread is using it.
  std::atomic<bool> events_healthy_{false};
  std::jthread event_thread_;
  mutable std::mutex error_mutex_;
  std::string last_error_;
};

} // namespace termusic
