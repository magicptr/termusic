#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/state.hpp"

namespace termusic::lyrics {

struct Line {
  double time_seconds = 0.0;
  std::string text;
};

struct Document {
  std::string title;
  std::string artist;
  std::filesystem::path source;
  std::vector<Line> lines;
  bool synchronized = false;

  /// Last timed line whose timestamp is not after `elapsed`, or -1 before the
  /// first line and for an empty/untimed document.
  int activeLine(double elapsed) const;
};

/// Parses UTF-8 LRC or ordinary text. Metadata tags and malformed timestamp
/// tags are ignored without discarding valid lyric lines around them.
Document parse(std::string_view content);

enum class LoadStatus { Found, NotFound, Unavailable, Error };

struct LoadResult {
  LoadStatus status = LoadStatus::NotFound;
  Document document;
  std::string message;

  bool found() const { return status == LoadStatus::Found; }
};

/// Loads sidecar lyrics for a local MPD song, constrained to `library_root`.
/// Candidates are `<stem>.lrc`, `<filename>.lrc`, then `<stem>.txt`.
class LocalProvider {
public:
  LoadResult load(const Song &song,
                  const std::filesystem::path &library_root) const;
};

} // namespace termusic::lyrics
