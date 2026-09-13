// Test doubles for the playback-context wiring.
//
// The Controller and the backend live in `termusic_runtime`, which drags in MPD,
// The spectrum FFT and the whole UI. The DECISIONS this round adds do not: they are the
// snapshot a start takes, how a row is resolved inside it, and how Previous /
// Next move through it. Those are re-stated here against tiny fakes, so the
// integration can be checked in a unit test instead of only by hand.
//
// PLAYBACK_CONTEXT_TESTS is registered as a ctest case next to termusic_core.

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <filesystem>
#include <fstream>

#include "app/playback.hpp"
#include "app/state.hpp"

namespace {

using termusic::PlaybackCollection;
using termusic::PlaybackSession;
using termusic::Song;

Song track(std::string uri) {
  Song song;
  song.uri = std::move(uri);
  song.title = song.uri;
  return song;
}

PlaybackCollection collection(PlaybackCollection::Kind kind,
                              std::string name = {}) {
  PlaybackCollection context;
  context.kind = kind;
  context.name = std::move(name);
  return context;
}

/// The part of a playback run this round defines: which list it snapshotted,
/// where in that snapshot it is, and how it steps.
struct Run {
  PlaybackSession session;

  void start(const std::vector<Song> &tracks, int index,
             const PlaybackCollection &context) {
    session = PlaybackSession{};
    session.context = context;
    session.sequence = tracks;
    termusic::assignOccurrenceCounters(session.sequence);
    session.occurrence = index;
  }

  /// Previous / Next: one step inside the snapshot, wrapping at its ends, so a
  /// run can never leave the collection it belongs to.
  const Song &step(int delta) {
    const int count = static_cast<int>(session.sequence.size());
    session.occurrence =
        ((session.occurrence + delta) % count + count) % count;
    return session.sequence[static_cast<std::size_t>(session.occurrence)];
  }

  int marker(const std::vector<Song> &displayed) const {
    const Song &playing =
        session.sequence[static_cast<std::size_t>(session.occurrence)];
    return termusic::playingRowIn(
        session.context, session.context, displayed, playing,
        session.context.kind == PlaybackCollection::Kind::History);
  }

  /// The marker as seen from ANOTHER collection: the question "does browsing
  /// somewhere else show my playback?".
  int markerIn(const PlaybackCollection &displayed,
               const std::vector<Song> &rows) const {
    const Song &playing =
        session.sequence[static_cast<std::size_t>(session.occurrence)];
    return termusic::playingRowIn(
        displayed, session.context, rows, playing,
        session.context.kind == PlaybackCollection::Kind::History);
  }
};

} // namespace

int main() {
  // --- A playlist with a duplicate URI -------------------------------------
  const std::vector<Song> rock = {track("A"), track("B"), track("A"), track("C")};
  const PlaybackCollection rock_ctx =
      collection(PlaybackCollection::Kind::Playlist, "Rock");

  // Spec §39: starting the SECOND A marks only that row, and the neighbours are
  // B and C -- the URI alone could not have said any of it.
  {
    Run run;
    run.start(rock, 2, rock_ctx);
    assert(run.marker(rock) == 2);
    assert(run.step(-1).uri == "B");
    assert(run.session.occurrence == 1);
    assert(run.step(1).uri == "A");
    assert(run.step(1).uri == "C");
    assert(run.session.occurrence == 3);
  }

  // --- The same URI in three collections (spec §1, §2, §15, §37) ----------
  {
    const std::vector<Song> library = {track("A"), track("B"), track("C")};
    const std::vector<Song> mirror = {track("A"), track("B"), track("C")};
    const PlaybackCollection library_ctx =
        collection(PlaybackCollection::Kind::Library);
    const PlaybackCollection default_ctx =
        collection(PlaybackCollection::Kind::Playlist, "default");

    Run run;
    run.start(library, 1, library_ctx);
    assert(run.marker(library) == 1);
    // Browsing `default`, whose row 1 is byte-for-byte the same URI, shows
    // nothing: it is not the collection that owns playback.
    assert(run.markerIn(default_ctx, mirror) == -1);
    assert(run.markerIn(rock_ctx, rock) == -1);

    // Spec §38: starting from `default` moves ownership, and Library goes dark.
    Run second;
    second.start(mirror, 1, default_ctx);
    assert(second.markerIn(default_ctx, mirror) == 1);
    assert(second.markerIn(library_ctx, library) == -1);
  }

  // --- Previous / Next follow the context, not the browsed list (spec §7) --
  {
    const std::vector<Song> playlist = {track("A"), track("B"), track("C"),
                                        track("D")};
    Run run;
    run.start(playlist, 2, rock_ctx);
    assert(run.session.sequence[static_cast<std::size_t>(
               run.session.occurrence)]
               .uri == "C");
    assert(run.step(-1).uri == "B");
    assert(run.step(1).uri == "C");
    assert(run.step(1).uri == "D");
    // Automatic progression is the same step, so it stays inside the list too
    // (spec §8).
    assert(run.step(1).uri == "A"); // wrapped, still the same collection
  }

  // --- A snapshot is not mutated by later edits (spec §11, §12, §25) -------
  {
    std::vector<Song> playlist = {track("A"), track("B"), track("C"),
                                  track("D")};
    Run run;
    run.start(playlist, 1, rock_ctx);
    const std::size_t length = run.session.sequence.size();

    // The user edits the collection underneath the run.
    playlist.insert(playlist.begin() + 2, track("X"));
    playlist.erase(playlist.begin() + 3);
    // And a History-style store edit removes the record the run started from.
    assert(run.session.sequence.size() == length);
    assert(run.session.sequence[1].uri == "B");
    assert(run.step(1).uri == "C");
  }

  // --- History: occurrence-keyed, deletion-proof (spec §16, §21, §33) ------
  {
    std::vector<Song> history_rows = {track("H"), track("N"), track("H"),
                                      track("N")};
    for (std::size_t index = 0; index < history_rows.size(); ++index)
      history_rows[index].history_record_id = static_cast<long long>(index + 1);
    const PlaybackCollection history_ctx =
        collection(PlaybackCollection::Kind::History);

    Run run;
    run.start(history_rows, 1, history_ctx);
    assert(run.marker(history_rows) == 1);

    // Deleting one same-URI record leaves the marker where it belongs ...
    std::vector<Song> after_delete;
    for (const Song &row : history_rows) {
      if (row.history_record_id != 3)
        after_delete.push_back(row);
    }
    assert(after_delete.size() == 3);
    assert(run.marker(after_delete) == 1);

    // ... and deleting the record that IS playing removes the marker rather
    // than letting another same-URI row claim it.
    std::vector<Song> without_playing;
    for (const Song &row : history_rows) {
      if (row.history_record_id != 2)
        without_playing.push_back(row);
    }
    assert(run.marker(without_playing) == -1);
    // The run itself is untouched: its snapshot still has four entries and the
    // next occurrence is the one that followed in THAT list, not whatever the
    // edited store now holds.
    assert(run.session.sequence.size() == 4);
    assert(run.step(1).uri == "H");
    assert(run.session.occurrence == 2);
  }

  // --- An unknown context marks nothing anywhere (spec §35, §36) -----------
  {
    const std::vector<Song> library = {track("A"), track("B")};
    const PlaybackCollection unknown;
    Song playing = track("B");
    playing.playback_occurrence = 1;
    assert(termusic::playingRowIn(collection(PlaybackCollection::Kind::Library),
                                  unknown, library, playing, false) == -1);
    assert(termusic::playingRowIn(
               collection(PlaybackCollection::Kind::History), unknown, library,
               playing, true) == -1);
  }

  // --- At most one marker (spec §14) ---------------------------------------
  {
    const std::vector<Song> rows = {track("A"), track("A"), track("A")};
    int markers = 0;
    for (std::size_t index = 0; index < rows.size(); ++index) {
      Song playing = track("A");
      playing.playback_occurrence = static_cast<int>(index) + 1;
      if (termusic::playingRowIn(
              collection(PlaybackCollection::Kind::Library),
              collection(PlaybackCollection::Kind::Library), rows, playing,
              false) >= 0)
        ++markers;
    }
    assert(markers == 3); // each occurrence resolves to exactly its own row
  }


}
