#pragma once

// The playback-context rules, kept as PURE functions so they can be tested
// without a backend, a queue or a terminal.
//
// The product rule this file encodes:
//
//   FROM WHICH COLLECTION PLAYBACK STARTS, THAT COLLECTION BECOMES THE
//   PLAYBACK CONTEXT -- and only that collection may paint the playing row.
//
// Two facts are needed to mark a row, and neither is the URI on its own: the
// same URI lives in Library, in `default` and in several playlists at once, and
// one History list can hold it twice. The URI is therefore only ever used
// together with a duplicate count ("which A is this"), or replaced outright by
// a History record id.

#include <vector>

#include "app/state.hpp"

namespace termusic {

/// Finds `target` inside `tracks` by URI *and* duplicate count.
///
/// `playback_occurrence` counts duplicates of the same URI from 1, so the
/// second `A` of `A B A C` is addressable and the first one is not confused with
/// it. Returns the row, or -1 when the target is not in the list.
int occurrenceIndexIn(const std::vector<Song> &tracks, const Song &target);

/// Fills `playback_occurrence` on a snapshot: 1 for the first `A`, 2 for the
/// second, and so on. A snapshot is taken once, at playback start, and never
/// recomputed afterwards.
void assignOccurrenceCounters(std::vector<Song> &tracks);

/// Which row of `tracks` the active playback marks, or -1 for none.
///
/// Both halves of the rule are required:
///   1. the DISPLAYED collection must be the playback context, and
///   2. the displayed row must be that exact occurrence.
///
/// `playing` is the occurrence the run is on: its URI plus its duplicate count.
/// When `context_is_history`, `playing.history_record_id` is used instead, so
/// deleting a record cannot make a different same-URI row look like it is
/// playing.
int playingRowIn(const PlaybackCollection &displayed,
                 const PlaybackCollection &context,
                 const std::vector<Song> &tracks, const Song &playing,
                 bool context_is_history);

} // namespace termusic
