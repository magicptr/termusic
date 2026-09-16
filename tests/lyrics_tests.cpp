#include <cassert>
#include <filesystem>
#include <fstream>

#include "lyrics/lyrics.hpp"

int main() {
  using namespace termusic;
  using namespace termusic::lyrics;

  const Document timed = parse(
      "[ti:Night Song]\n[ar:Example]\n[offset:500]\n"
      "[00:10.00][00:20.00]Hello\n[00:15.25]World\n");
  assert(timed.synchronized);
  assert(timed.title == "Night Song");
  assert(timed.artist == "Example");
  assert(timed.lines.size() == 3);
  assert(timed.lines[0].time_seconds == 10.5);
  assert(timed.lines[1].time_seconds == 15.75);
  assert(timed.lines[2].time_seconds == 20.5);
  assert(timed.activeLine(10.49) == -1);
  assert(timed.activeLine(10.5) == 0);
  assert(timed.activeLine(19.0) == 1);
  assert(timed.activeLine(99.0) == 2);

  const Document plain = parse("First line\n\nSecond line\n");
  assert(!plain.synchronized);
  assert(plain.lines.size() == 2);
  assert(plain.activeLine(10.0) == -1);

  const auto root = std::filesystem::temp_directory_path() /
                    "termusic-lyrics-test-library";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "Artist" / "Album");
  const auto lyrics_path = root / "Artist" / "Album" / "Track.lrc";
  {
    std::ofstream output(lyrics_path);
    output << "[00:01.00]Loaded from disk\n";
  }

  Song song;
  song.uri = "Artist/Album/Track.flac";
  LocalProvider local;
  const LoadResult loaded = local.load(song, root);
  assert(loaded.found());
  assert(loaded.document.source == lyrics_path);
  assert(loaded.document.lines.front().text == "Loaded from disk");

  Song traversal;
  traversal.uri = "../outside.flac";
  assert(local.load(traversal, root).status == LoadStatus::Unavailable);
  Song remote;
  remote.uri = "https://radio.example/live";
  remote.source_id = "direct-url";
  assert(local.load(remote, root).status == LoadStatus::Unavailable);
  std::filesystem::remove_all(root);
}
