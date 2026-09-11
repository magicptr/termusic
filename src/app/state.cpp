#include "app/state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

#include "util/text.hpp"

namespace termusic {

std::string Song::displayTitle() const {
  if (!title.empty()) {
    return title;
  }
  return util::basename(uri);
}

std::string Song::displayArtist() const {
  if (!artist.empty()) {
    return artist;
  }
  return "Unknown Artist";
}

std::string Song::displayAlbum() const {
  if (!album.empty()) {
    return album;
  }
  return "Unknown Album";
}

double effectiveElapsed(const PlayerState &player,
                        std::chrono::steady_clock::time_point now) {
  if (player.state != PlaybackState::Playing) {
    return player.elapsed_seconds;
  }
  if (player.sampled_at.time_since_epoch().count() == 0) {
    return player.elapsed_seconds;
  }

  const std::chrono::duration<double> since = now - player.sampled_at;
  double elapsed = player.elapsed_seconds + since.count();
  if (elapsed < 0.0) {
    elapsed = 0.0;
  }
  if (player.duration_seconds > 0.0) {
    elapsed = std::min(elapsed, player.duration_seconds);
  }
  return elapsed;
}

double progressRatio(const PlayerState &player,
                     std::chrono::steady_clock::time_point now) {
  if (player.duration_seconds <= 0.0) {
    return 0.0;
  }
  const double ratio = effectiveElapsed(player, now) / player.duration_seconds;
  return std::clamp(ratio, 0.0, 1.0);
}

TrackRef trackRefFromSong(const Song &song) {
  TrackRef ref;
  ref.uri = song.uri;
  ref.title = song.displayTitle();
  ref.artist = song.displayArtist();
  ref.album = song.album;
  return ref;
}

// User-facing names for the top-level sections. These are DISPLAY strings
// only: the Page enum keeps its Library/Settings spelling everywhere in the
// code, and the media collection inside the Vault is still called "Library".
// "Vault" therefore names the page, not the collection it opens.
std::string_view pageLabel(Page page) {
  switch (page) {
  case Page::Library:
    return "Vault";
  case Page::Settings:
    return "Core";
  }
  return "Vault";
}

std::string_view pageShortLabel(Page page) {
  switch (page) {
  case Page::Library:
    return "Vlt";
  case Page::Settings:
    return "Cor";
  }
  return "Vlt";
}

std::string_view pageToken(Page page) {
  switch (page) {
  case Page::Library:
    return "library";
  case Page::Settings:
    return "settings";
  }
  return "library";
}

std::optional<Page> pageFromToken(std::string_view token) {
  if (token == "library")
    return Page::Library;
  if (token == "settings")
    return Page::Settings;
  // "files" is retired: the media database is Library -> Default now.
  if (token == "files")
    return Page::Library;
  // Legacy tokens from the removed top-level pages. They must still load so an
  // existing config file is not rejected, but they all resolve to Library:
  // Nothing Playing, Queue and Playlists are Library views or tree nodes now.
  if (token == "now_playing" || token == "now-playing" || token == "playlist" ||
      token == "queue" || token == "search" || token == "lyrics" ||
      token == "visualizer" || token == "visualiser" || token == "equalizer" ||
      token == "equaliser")
    return Page::Library;
  return std::nullopt;
}

const std::vector<Page> &allPages() {
  // The only navigable sections. Queue / Playlists live in the workspace tree,
  // Lyrics and the spectrum live in the Now Playing view, and the equalizer
  // belongs to configuration -- none of them are destinations any more.
  // The only two top-level sections. The media database and the queue are
  // Library collections, not destinations of their own.
  static const std::vector<Page> pages = {Page::Library, Page::Settings};
  return pages;
}


namespace {

struct ReferenceTrack {
  const char *title;
  const char *album;
  const char *duration;
};

constexpr ReferenceTrack kReferenceQueue[] = {
    {"七里香", "七里香", "4:59"},         {"一路向北", "十一月的萧邦", "4:17"},
    {"不能说的秘密", "不能说的秘密", "4:56"}, {"晴天", "叶惠美", "4:29"},
    {"搁浅", "七里香", "4:15"},           {"发如雪", "十一月的萧邦", "3:42"},
    {"告白气球", "周杰伦的床边故事", "3:35"}, {"稻香", "魔杰座", "3:43"},
    {"夜曲", "十一月的萧邦", "3:46"},     {"兰亭序", "魔杰座", "4:13"},
    {"青花瓷", "我很忙", "3:59"},         {"简单爱", "范特西", "4:30"},
    {"轨迹", "寻找周杰伦", "4:26"},       {"彩虹", "我很忙", "4:23"},
    {"说好的幸福呢", "魔杰座", "4:16"},   {"明明就", "十二新作", "4:21"},
    {"等你下课", "等你下课", "4:30"},     {"算什么男人", "十二新作", "4:09"},
    {"Mojito", "Mojito", "3:05"},         {"圣诞星", "十二新作", "4:18"},
    {"我是如此相信", "我是如此相信", "4:26"}, {"夜的第七章", "依然范特西", "3:48"},
    {"龙卷风", "范特西", "4:10"},         {"爱在西元前", "范特西", "3:54"},
    {"爷爷泡的茶", "八度空间", "4:01"},   {"半岛铁盒", "八度空间", "5:19"},
    {"暗号", "八度空间", "4:31"},         {"以父之名", "叶惠美", "5:42"},
};

constexpr const char *kReferenceArtist = "周杰伦";

double parseDuration(const char *text) {
  int minutes = 0;
  int seconds = 0;
  if (std::sscanf(text, "%d:%d", &minutes, &seconds) != 2)
    return 0.0;
  return static_cast<double>(minutes * 60 + seconds);
}

} // namespace

double displayElapsed(const AppState &state,
                      std::chrono::steady_clock::time_point now) {
  if (state.demo)
    return state.player.elapsed_seconds;
  return effectiveElapsed(state.player, now);
}

double displayProgress(const AppState &state,
                       std::chrono::steady_clock::time_point now) {
  if (state.player.duration_seconds <= 0.0)
    return 0.0;
  return std::clamp(displayElapsed(state, now) / state.player.duration_seconds,
                    0.0, 1.0);
}

void loadReferenceState(AppState &state) {
  state.queue.clear();
  state.queue.reserve(std::size(kReferenceQueue));
  unsigned id = 1;
  for (const ReferenceTrack &track : kReferenceQueue) {
    Song song;
    song.uri = std::string(kReferenceArtist) + "/" + track.album + "/" +
               track.title + ".flac";
    song.title = track.title;
    song.artist = kReferenceArtist;
    song.album = track.album;
    song.duration_seconds = parseDuration(track.duration);
    song.queue_id = id;
    song.queue_position = id - 1;
    state.queue.push_back(std::move(song));
    ++id;
  }
  state.queue_visible.resize(state.queue.size());
  for (std::size_t index = 0; index < state.queue.size(); ++index)
    state.queue_visible[index] = static_cast<int>(index);

  constexpr std::size_t kReferenceIndex = 4;
  const Song &current = state.queue[kReferenceIndex];
  state.selected_queue = static_cast<int>(kReferenceIndex);
  state.player.state = PlaybackState::Playing;
  state.player.current_song = current;
  state.player.current_song_id = static_cast<int>(*current.queue_id);
  state.player.elapsed_seconds = 137.0;
  state.player.duration_seconds = current.duration_seconds;
  state.player.volume = 75;
  state.player.repeat = false;
  state.player.random = false;
  state.player.sampled_at = std::chrono::steady_clock::now();
  state.mpd_connected = true;
  // The demo fixture is a rendering fixture only: it must not choose a
  // section. Startup belongs to the canonical model.
  state.page = Page::Library;
  state.presentation = PresentationMode::Normal;
  state.error.reset();
  state.toast.reset();
  loadReferenceSpectrum(state.visualizer, 44);
}

void loadReferenceSpectrum(VisualizerState &visualizer, std::size_t bands) {
  visualizer.bars.assign(bands, 0.0F);
  visualizer.peaks.assign(bands, 0.0F);
  if (bands == 0)
    return;
  const auto gaussian = [](double x, double centre, double width) {
    const double d = (x - centre) / width;
    return std::exp(-0.5 * d * d);
  };
  for (std::size_t index = 0; index < bands; ++index) {
    const double x = static_cast<double>(index) /
                     static_cast<double>(bands > 1 ? bands - 1 : 1);
    double value = 0.88 * std::exp(-2.9 * x);
    value += 0.30 * gaussian(x, 0.20, 0.045);
    value += 0.26 * gaussian(x, 0.36, 0.035);
    value += 0.20 * gaussian(x, 0.55, 0.030);
    value += 0.14 * gaussian(x, 0.74, 0.025);
    value += 0.05 * std::sin(x * 41.0);
    const auto level = static_cast<float>(std::clamp(value, 0.0, 1.0));
    visualizer.bars[index] = level;
    // Peaks belong only to the strong bands, as in the reference: a held peak
    // on every band would draw a dotted line across the whole spectrum.
    visualizer.peaks[index] =
        level > 0.34F ? std::min(1.0F, level + 0.10F) : level;
  }
  visualizer.data_available = true;
  visualizer.enabled = true;
}

} // namespace termusic

