#pragma once

// Playback History: the latest 100 tracks that actually started playing.
//
// This is deliberately NOT an MPD saved playlist. History allows duplicate
// entries, has a fixed length and is application metadata, so storing it in
// MPD would pollute the user's Playlists with a list they cannot edit. It
// lives in the application's own state directory instead.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "app/state.hpp"

namespace termusic {

/// One recorded playback occurrence.
///
/// `uri` is the canonical identity. The tag fields are a render cache so that
/// History can be displayed without a database round trip; they are never used
/// to identify an entry.
struct HistoryEntry {
  /// Identity of this OCCURRENCE, unique within the store. Two entries with
  /// the same URI are different records with different ids, which is what makes
  /// "delete the second A" expressible at all -- a URI is not an identity here.
  long long id = 0;
  std::string uri;
  std::string source_id;
  std::string source_track_id;
  bool is_live_stream = false;
  std::string title;
  std::string artist;
  std::string album;
  /// When the track started playing, as Unix epoch seconds. 0 means the
  /// occurrence was recorded before the file carried timestamps; the History
  /// view then shows "--" rather than a fabricated date.
  long long played_at = 0;
};

class HistoryStore {
public:
  /// Oldest entries are evicted once this many have been recorded. The cap is
  /// configurable ([history] max_entries); this is the default.
  static constexpr std::size_t kMaxEntries = 100;

  /// An explicit path is used EXACTLY as given (tests, tools). The default
  /// constructor resolves the XDG data path and additionally accepts history
  /// written by an older termusic in the legacy state directory -- an explicit
  /// path never reaches outside itself, so a test can never read the user's
  /// real files.
  explicit HistoryStore(std::filesystem::path path = {});

  /// `$XDG_DATA_HOME/termusic/history.toml`, else
  /// `~/.local/share/termusic/history.toml`: application-owned data lives in
  /// the data directory, never in an MPD-owned one.
  static std::filesystem::path defaultPath();

  /// Bounds the store. Applied immediately, so lowering the cap below the
  /// current size evicts the oldest entries at the next save.
  void setMaxEntries(std::size_t max_entries);
  std::size_t maxEntries() const { return max_entries_; }

  /// Never throws and never fails hard: a missing or malformed file simply
  /// leaves the history empty. The file is only written by save().
  bool load(std::string *warning = nullptr);
  bool save(std::string *error = nullptr) const;

  /// Appends one playback occurrence and evicts the oldest beyond the cap.
  /// Duplicates are kept: playing the same URI again is a new occurrence.
  /// The occurrence is stamped with the wall clock and given a fresh id.
  void record(const Song &song);

  /// record() with an explicit play time, so tests can assert on stored
  /// timestamps without depending on the machine's clock.
  void recordAt(const Song &song, long long played_at);

  /// Removes one occurrence by its id. Returns false when the id is unknown.
  ///
  /// This is application-owned storage ONLY: no file, no MPD database entry,
  /// no saved playlist and no internal queue item is touched. Two entries that
  /// share a URI are still independent records.
  bool removeRecord(long long id);

  /// Removes `count` occurrences starting at `first` (storage order, oldest
  /// first). Positions are resolved against the current contents in one pass,
  /// so a caller deleting a contiguous visual range never has to compensate
  /// for the indices shifting under it. Returns how many were removed.
  int removeRange(std::size_t first, std::size_t count);

  /// True when this id belongs to a record that is still stored.
  bool hasRecord(long long id) const;

  /// The id of the occurrence at `index` (storage order), or 0 when out of
  /// range.
  long long recordIdAt(std::size_t index) const;

  /// Storage order: oldest first. Unambiguous and independent of how the UI
  /// chooses to display it.
  const std::vector<HistoryEntry> &entries() const { return entries_; }
  std::size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }

  /// Display order: newest first, which is what a "recently played" view
  /// wants. Row 0 is therefore the most recent occurrence.
  std::vector<Song> songsNewestFirst() const;

  /// The play times of songsNewestFirst(), in the same row order, so a view
  /// can show "title, artist, played at" without re-reading the entries.
  std::vector<long long> timesNewestFirst() const;

  /// Bumped by every mutation, so views can rebuild only when it changes.
  std::size_t revision() const { return revision_; }

  /// Bumped only when the WHOLE store is replaced (load, clear). A playback
  /// session started from History is tied to one epoch: replacing the store
  /// invalidates that context instead of letting a stale id claim a marker.
  long long epoch() const { return epoch_; }

  void clear();

private:
  std::filesystem::path path_;
  /// True only for the default constructor: enables the legacy read.
  bool legacy_fallback_ = false;
  std::size_t max_entries_ = kMaxEntries;
  std::vector<HistoryEntry> entries_;
  std::size_t revision_ = 0;
  /// Last id handed out. Every occurrence gets a fresh one, so an id is never
  /// reused even after deletions or a reload.
  long long next_id_ = 1;
  long long epoch_ = 1;
};

} // namespace termusic
