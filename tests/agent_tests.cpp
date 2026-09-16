#include <cassert>
#include <string>
#include <vector>

#include "agent/music_agent.hpp"

namespace {

termusic::Song song(std::string uri, std::string title, std::string artist,
                    std::string album = {}, std::string genre = {}) {
  termusic::Song value;
  value.uri = std::move(uri);
  value.title = std::move(title);
  value.artist = std::move(artist);
  value.album = std::move(album);
  value.genre = std::move(genre);
  return value;
}

} // namespace

int main() {
  using namespace termusic;
  using namespace termusic::agent;

  assert(MusicAgent::extractSearchText("请帮我播放 夜曲") == "夜曲");
  assert(MusicAgent::extractSearchText("play jazz music") == "jazz");
  assert(MusicAgent::extractSearchText("Melody") == "melody");
  const QueryIntent local_intent =
      MusicAgent::parseIntent("请播放本地 jazz 音乐");
  assert(local_intent.search_text == "jazz");
  assert(local_intent.scope == SourceScope::LocalOnly);
  assert(local_intent.autoplay);
  const QueryIntent stream_intent =
      MusicAgent::parseIntent("search stream only night");
  assert(stream_intent.search_text == "night");
  assert(stream_intent.scope == SourceScope::StreamOnly);
  assert(!stream_intent.autoplay);

  std::vector<Song> local = {
      song("local/night.flac", "Night Song", "Alice", "Moon", "Jazz"),
      song("local/day.flac", "Sunny Day", "Bob", "Sky", "Pop"),
  };
  std::vector<Song> streams = {
      song("https://radio.example/jazz", "Night Jazz Radio", "Radio"),
      song("https://radio.example/news", "Daily News", "Radio"),
  };
  streams[0].source_id = "direct-url";
  streams[0].source_track_id = "jazz";
  streams[1].source_id = "direct-url";
  streams[1].source_track_id = "news";

  MusicAgent agent;
  const QueryResult result = agent.query("find night", local, streams);
  assert(result.search_text == "night");
  assert(result.candidates.size() == 2);
  assert(result.local_matches == 1);
  assert(result.stream_matches == 1);
  assert(result.candidates[0].song.title == "Night Song");
  assert(result.candidates[0].source == MusicSource::Local);
  assert(result.candidates[1].source == MusicSource::Stream);

  const QueryResult genre = agent.query("来点 jazz 音乐", local, streams, 1);
  assert(genre.candidates.size() == 1);
  // A title match outranks a genre-only match, regardless of source.
  assert(genre.candidates.front().song.uri ==
         "https://radio.example/jazz");
  assert(genre.local_matches == 0);
  assert(genre.stream_matches == 1);

  assert(agent.query("classical", local, streams).candidates.empty());

  const QueryResult local_only = agent.query("播放本地 night", local, streams);
  assert(local_only.intent.autoplay);
  assert(local_only.candidates.size() == 1);
  assert(local_only.candidates.front().source == MusicSource::Local);

  const QueryResult stream_only =
      agent.query("search stream only night", local, streams);
  assert(!stream_only.intent.autoplay);
  assert(stream_only.candidates.size() == 1);
  assert(stream_only.candidates.front().source == MusicSource::Stream);
}
