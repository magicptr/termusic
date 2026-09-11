#pragma once

// Phase C: the one unnamed music register used by the Vim-style
// select -> yank -> paste workflow.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace termusic {

/// A stable reference to one piece of music.
///
/// The URI is the canonical identity and the only field any backend mutation
/// uses. The metadata is carried purely so the UI can describe the register
/// without re-querying MPD.
///
/// A queue id is deliberately NOT part of this type: appending a track to the
/// queue assigns it a brand new id, so a remembered id says nothing about the
/// appended instance. Queue ids remain useful only for identifying an item
/// that is already in the queue.
struct TrackRef {
  std::string uri;
  std::string title;
  std::string artist;
  std::string album;

  bool empty() const { return uri.empty(); }
};

/// The single unnamed music register. Deliberately minimal: no named
/// registers, no clipboard integration, no register history.
struct MusicRegister {
  std::vector<TrackRef> tracks;

  bool empty() const { return tracks.empty(); }
  std::size_t size() const { return tracks.size(); }

  void set(std::vector<TrackRef> refs) { tracks = std::move(refs); }
  void clear() { tracks.clear(); }
};

} // namespace termusic
