#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "app/state.hpp"

namespace termusic::agent {

enum class MusicSource { Local, Stream };
enum class SourceScope { All, LocalOnly, StreamOnly };

struct QueryIntent {
  std::string search_text;
  SourceScope scope = SourceScope::All;
  /// A request containing an explicit listening verb selects and starts the
  /// highest-ranked result; a neutral search only presents candidates.
  bool autoplay = false;
};

struct Candidate {
  Song song;
  MusicSource source = MusicSource::Local;
  int score = 0;
  std::string reason;
};

struct QueryResult {
  std::string prompt;
  /// The music keyword left after removing common command words such as
  /// "play", "find" and their Chinese equivalents.
  std::string search_text;
  std::vector<Candidate> candidates;
  std::size_t local_matches = 0;
  std::size_t stream_matches = 0;
  QueryIntent intent;
};

struct StreamingCatalogResult {
  std::vector<Song> songs;
  std::vector<std::string> warnings;
};

/// Provider-independent music selection core. It deliberately has no UI,
/// network or MPD dependency: a future LLM planner can produce the query while
/// this module remains the deterministic catalog merger and ranker.
class MusicAgent {
public:
  static QueryIntent parseIntent(std::string_view prompt);
  static std::string extractSearchText(std::string_view prompt);

  QueryResult query(std::string_view prompt, const std::vector<Song> &local,
                    const std::vector<Song> &streams,
                    std::size_t limit = 100) const;
};

const char *sourceLabel(MusicSource source);

} // namespace termusic::agent
