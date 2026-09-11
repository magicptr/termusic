#include "app/playback.hpp"

#include <map>
#include <string>

namespace termusic {

int occurrenceIndexIn(const std::vector<Song> &tracks, const Song &target) {
  if (target.uri.empty())
    return -1;
  const int wanted = target.playback_occurrence > 0 ? target.playback_occurrence
                                                    : 1;
  int seen = 0;
  for (std::size_t index = 0; index < tracks.size(); ++index) {
    if (tracks[index].uri != target.uri)
      continue;
    if (++seen == wanted)
      return static_cast<int>(index);
  }
  return -1;
}

void assignOccurrenceCounters(std::vector<Song> &tracks) {
  std::map<std::string, int> seen;
  for (Song &song : tracks)
    song.playback_occurrence = ++seen[song.uri];
}

int playingRowIn(const PlaybackCollection &displayed,
                 const PlaybackCollection &context,
                 const std::vector<Song> &tracks, const Song &playing,
                 bool context_is_history) {
  if (tracks.empty() || !context.valid())
    return -1;
  // Half one: the list on screen must be the list that owns playback. Browsing
  // another collection therefore shows NO marker, even when the URIs match.
  if (displayed != context)
    return -1;
  if (context_is_history) {
    // Occurrence identity, never the URI: two History rows may share a URI and
    // only the record that is playing may carry the marker. A record that was
    // deleted while it keeps playing has no row here, and fabricating one onto
    // another same-URI record would claim the wrong row is playing.
    if (playing.history_record_id == 0)
      return -1;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
      if (tracks[index].history_record_id != 0 &&
          tracks[index].history_record_id == playing.history_record_id)
        return static_cast<int>(index);
    }
    return -1;
  }
  // Half two: the exact occurrence. The duplicate count is what separates the
  // second `A` from the first.
  return occurrenceIndexIn(tracks, playing);
}

} // namespace termusic
