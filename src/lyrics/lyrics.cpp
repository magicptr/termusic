#include "lyrics/lyrics.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>

namespace termusic::lyrics {
namespace {

std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::optional<double> timestamp(std::string_view tag) {
  const auto colon = tag.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon + 1 >= tag.size())
    return std::nullopt;
  int minutes = 0;
  const auto minute = std::from_chars(tag.data(), tag.data() + colon, minutes);
  if (minute.ec != std::errc{} || minute.ptr != tag.data() + colon ||
      minutes < 0)
    return std::nullopt;
  double seconds = 0.0;
  try {
    const std::string value(tag.substr(colon + 1));
    std::size_t used = 0;
    seconds = std::stod(value, &used);
    if (used != value.size())
      return std::nullopt;
  } catch (const std::exception &) {
    return std::nullopt;
  }
  if (!std::isfinite(seconds) || seconds < 0.0 || seconds >= 60.0)
    return std::nullopt;
  return static_cast<double>(minutes) * 60.0 + seconds;
}

bool inside(const std::filesystem::path &root,
            const std::filesystem::path &candidate) {
  auto root_part = root.begin();
  auto candidate_part = candidate.begin();
  for (; root_part != root.end(); ++root_part, ++candidate_part) {
    if (candidate_part == candidate.end() || *root_part != *candidate_part)
      return false;
  }
  return true;
}

} // namespace

int Document::activeLine(double elapsed) const {
  if (!synchronized || lines.empty())
    return -1;
  const auto after = std::upper_bound(
      lines.begin(), lines.end(), elapsed,
      [](double value, const Line &line) { return value < line.time_seconds; });
  return after == lines.begin()
             ? -1
             : static_cast<int>(std::distance(lines.begin(), after) - 1);
}

Document parse(std::string_view content) {
  Document result;
  std::vector<Line> untimed;
  double offset_seconds = 0.0;
  std::istringstream input{std::string(content)};
  std::string row;
  while (std::getline(input, row)) {
    if (!row.empty() && row.back() == '\r')
      row.pop_back();
    std::vector<double> times;
    std::size_t position = 0;
    while (position < row.size() && row[position] == '[') {
      const auto close = row.find(']', position + 1);
      if (close == std::string::npos)
        break;
      const std::string_view tag(row.data() + position + 1,
                                 close - position - 1);
      if (const auto at = timestamp(tag); at.has_value()) {
        times.push_back(*at);
      } else if (tag.starts_with("ti:")) {
        result.title = std::string(tag.substr(3));
      } else if (tag.starts_with("ar:")) {
        result.artist = std::string(tag.substr(3));
      } else if (tag.starts_with("offset:")) {
        try {
          offset_seconds = std::stod(std::string(tag.substr(7))) / 1000.0;
        } catch (const std::exception &) {
          offset_seconds = 0.0;
        }
      }
      position = close + 1;
    }
    const std::string text = trim(std::string_view(row).substr(position));
    if (!times.empty()) {
      for (const double at : times)
        result.lines.push_back({at, text});
    } else if (!text.empty() && position == 0) {
      untimed.push_back({0.0, text});
    }
  }

  if (!result.lines.empty()) {
    result.synchronized = true;
    for (Line &line : result.lines)
      line.time_seconds = std::max(0.0, line.time_seconds + offset_seconds);
    std::stable_sort(result.lines.begin(), result.lines.end(),
                     [](const Line &lhs, const Line &rhs) {
                       return lhs.time_seconds < rhs.time_seconds;
                     });
  } else {
    result.lines = std::move(untimed);
  }
  return result;
}

LoadResult LocalProvider::load(
    const Song &song, const std::filesystem::path &library_root) const {
  if (!song.source_id.empty() || song.uri.empty()) {
    return {LoadStatus::Unavailable, {},
            "Local lyrics are unavailable for this source"};
  }
  if (library_root.empty())
    return {LoadStatus::Unavailable, {}, "Music library path is unavailable"};
  std::error_code error;
  const auto root = std::filesystem::weakly_canonical(library_root, error);
  if (error || root.empty())
    return {LoadStatus::Unavailable, {}, "Music library path is unavailable"};
  const std::filesystem::path relative(song.uri);
  if (relative.is_absolute())
    return {LoadStatus::Unavailable, {},
            "Absolute MPD paths are not read as local lyrics"};
  const auto audio = std::filesystem::weakly_canonical(root / relative, error);
  if (error || !inside(root, audio))
    return {LoadStatus::Unavailable, {},
            "Song path is outside the configured music library"};

  std::vector<std::filesystem::path> candidates;
  auto stem = audio;
  stem.replace_extension(".lrc");
  candidates.push_back(stem);
  candidates.emplace_back(audio.string() + ".lrc");
  stem = audio;
  stem.replace_extension(".txt");
  candidates.push_back(stem);

  for (const auto &candidate : candidates) {
    error.clear();
    const auto resolved_candidate =
        std::filesystem::weakly_canonical(candidate, error);
    if (error || !inside(root, resolved_candidate))
      continue;
    std::ifstream input(resolved_candidate, std::ios::binary);
    if (!input)
      continue;
    constexpr std::streamsize kMaxLyricsBytes = 2 * 1024 * 1024;
    std::string content;
    content.assign(std::istreambuf_iterator<char>(input),
                   std::istreambuf_iterator<char>());
    if (static_cast<std::streamsize>(content.size()) > kMaxLyricsBytes)
      return {LoadStatus::Error, {}, "Lyrics file is larger than 2 MiB"};
    Document document = parse(content);
    document.source = resolved_candidate;
    if (document.lines.empty())
      return {LoadStatus::Error, {}, "Lyrics file is empty"};
    return {LoadStatus::Found, std::move(document), {}};
  }
  return {LoadStatus::NotFound, {}, "No .lrc or .txt sidecar lyrics found"};
}

} // namespace termusic::lyrics
