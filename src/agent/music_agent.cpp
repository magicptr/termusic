#include "agent/music_agent.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <utility>

namespace termusic::agent {
namespace {

std::string trim(std::string value) {
  const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
  value.erase(value.begin(),
              std::find_if_not(value.begin(), value.end(), space));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(),
              value.end());
  return value;
}

std::string foldAscii(std::string value) {
  for (char &c : value) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return value;
}

void eraseAll(std::string &value, std::string_view word) {
  for (std::size_t position = value.find(word); position != std::string::npos;
       position = value.find(word))
    value.erase(position, word.size());
}

void eraseAsciiWord(std::string &value, std::string_view word) {
  const auto wordCharacter = [](char c) {
    const unsigned char byte = static_cast<unsigned char>(c);
    return std::isalnum(byte) != 0 || c == '_';
  };
  std::size_t position = 0;
  while ((position = value.find(word, position)) != std::string::npos) {
    const bool left_boundary =
        position == 0 || !wordCharacter(value[position - 1]);
    const std::size_t after = position + word.size();
    const bool right_boundary =
        after == value.size() || !wordCharacter(value[after]);
    if (left_boundary && right_boundary)
      value.replace(position, word.size(), " ");
    else
      position = after;
  }
}

bool containsAsciiWord(const std::string &value, std::string_view word) {
  const auto wordCharacter = [](char c) {
    const unsigned char byte = static_cast<unsigned char>(c);
    return std::isalnum(byte) != 0 || c == '_';
  };
  for (std::size_t position = value.find(word); position != std::string::npos;
       position = value.find(word, position + word.size())) {
    const bool left_boundary =
        position == 0 || !wordCharacter(value[position - 1]);
    const std::size_t after = position + word.size();
    const bool right_boundary =
        after == value.size() || !wordCharacter(value[after]);
    if (left_boundary && right_boundary)
      return true;
  }
  return false;
}

std::vector<std::string> terms(std::string_view search_text) {
  std::istringstream input{std::string(search_text)};
  std::vector<std::string> result;
  for (std::string term; input >> term;)
    result.push_back(foldAscii(std::move(term)));
  if (result.empty() && !search_text.empty())
    result.push_back(foldAscii(std::string(search_text)));
  return result;
}

int fieldScore(const std::string &field, const std::vector<std::string> &words,
               int exact, int prefix, int contains) {
  const std::string folded = foldAscii(field);
  int score = 0;
  for (const std::string &word : words) {
    if (word.empty())
      continue;
    if (folded == word)
      score += exact;
    else if (folded.starts_with(word))
      score += prefix;
    else if (folded.find(word) != std::string::npos)
      score += contains;
  }
  return score;
}

int score(const Song &song, const std::vector<std::string> &words) {
  if (words.empty())
    return 1;
  return fieldScore(song.displayTitle(), words, 120, 90, 70) +
         fieldScore(song.displayArtist(), words, 100, 75, 55) +
         fieldScore(song.displayAlbum(), words, 70, 50, 35) +
         fieldScore(song.genre, words, 60, 45, 30) +
         fieldScore(song.uri, words, 30, 20, 10);
}

std::string identity(const Song &song, MusicSource source) {
  if (source == MusicSource::Stream && !song.source_id.empty())
    return "stream:" + song.source_id + ":" + song.source_track_id;
  return "local:" + song.uri;
}

} // namespace

QueryIntent MusicAgent::parseIntent(std::string_view prompt) {
  QueryIntent intent;
  std::string value = trim(std::string(prompt));
  const std::string original = foldAscii(value);
  const bool local_requested = original.find("本地") != std::string::npos ||
                               original.find("只听本地") != std::string::npos ||
                               original.find("local only") != std::string::npos ||
                               original.find("only local") != std::string::npos;
  const bool stream_requested =
      original.find("流媒体") != std::string::npos ||
      original.find("只听 stream") != std::string::npos ||
      original.find("stream only") != std::string::npos ||
      original.find("only stream") != std::string::npos ||
      containsAsciiWord(original, "streams");
  if (local_requested != stream_requested)
    intent.scope = local_requested ? SourceScope::LocalOnly
                                   : SourceScope::StreamOnly;
  intent.autoplay = original.find("播放") != std::string::npos ||
                    original.find("我想听") != std::string::npos ||
                    original.find("来一首") != std::string::npos ||
                    original.find("来点") != std::string::npos ||
                    containsAsciiWord(original, "play");

  // Longer phrases come first so their shorter suffixes cannot leave debris.
  static constexpr std::string_view chinese_commands[] = {
      "请帮我播放", "帮我播放", "我想听", "来一首", "来点", "播放",
      "搜索",       "查找",     "找一下", "找",     "一些", "歌曲",
      "音乐",       "只听本地", "只搜本地", "本地", "只听流媒体",
      "只搜流媒体", "流媒体",   "请"};
  static constexpr std::string_view english_commands[] = {
      "please", "search", "find", "play", "song", "music", "for", "me"};
  std::string folded = foldAscii(value);
  for (const std::string_view command : chinese_commands)
    eraseAll(folded, command);
  for (const std::string_view command : english_commands)
    eraseAsciiWord(folded, command);
  if (intent.scope != SourceScope::All) {
    eraseAsciiWord(folded, "only");
    eraseAsciiWord(folded, "local");
    eraseAsciiWord(folded, "stream");
    eraseAsciiWord(folded, "streams");
  }
  // ASCII command words were removed from the folded copy. Chinese text is
  // byte-identical under ASCII folding, so one result handles both scripts.
  value = trim(std::move(folded));
  while (!value.empty() &&
         (value.front() == ':' || value.front() == '-' ||
          value.front() == ','))
    value.erase(value.begin());
  intent.search_text = trim(std::move(value));
  return intent;
}

std::string MusicAgent::extractSearchText(std::string_view prompt) {
  return parseIntent(prompt).search_text;
}

QueryResult MusicAgent::query(std::string_view prompt,
                              const std::vector<Song> &local,
                              const std::vector<Song> &streams,
                              std::size_t limit) const {
  QueryResult result;
  result.prompt = std::string(prompt);
  result.intent = parseIntent(prompt);
  result.search_text = result.intent.search_text;
  const std::vector<std::string> words = terms(result.search_text);
  std::set<std::string> seen;

  const auto collect = [&](const std::vector<Song> &songs,
                           MusicSource source) {
    if ((result.intent.scope == SourceScope::LocalOnly &&
         source != MusicSource::Local) ||
        (result.intent.scope == SourceScope::StreamOnly &&
         source != MusicSource::Stream))
      return;
    for (const Song &song : songs) {
      const int rank = score(song, words);
      if (rank <= 0 || !seen.insert(identity(song, source)).second)
        continue;
      Candidate candidate;
      candidate.song = song;
      candidate.source = source;
      candidate.score = rank;
      candidate.reason = source == MusicSource::Local
                             ? "matched the local library"
                             : "matched Streams";
      result.candidates.push_back(std::move(candidate));
      if (source == MusicSource::Local)
        ++result.local_matches;
      else
        ++result.stream_matches;
    }
  };
  collect(local, MusicSource::Local);
  collect(streams, MusicSource::Stream);
  std::stable_sort(result.candidates.begin(), result.candidates.end(),
                   [](const Candidate &lhs, const Candidate &rhs) {
                     if (lhs.score != rhs.score)
                       return lhs.score > rhs.score;
                     if (lhs.source != rhs.source)
                       return lhs.source == MusicSource::Local;
                     return lhs.song.displayTitle() < rhs.song.displayTitle();
                   });
  if (result.candidates.size() > limit)
    result.candidates.resize(limit);
  // Counts describe the visible, playable response, not discarded tail rows.
  result.local_matches = 0;
  result.stream_matches = 0;
  for (const Candidate &candidate : result.candidates) {
    if (candidate.source == MusicSource::Local)
      ++result.local_matches;
    else
      ++result.stream_matches;
  }
  return result;
}

const char *sourceLabel(MusicSource source) {
  return source == MusicSource::Local ? "Local" : "Stream";
}

} // namespace termusic::agent
