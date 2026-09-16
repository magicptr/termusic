#include "streaming/stream_store.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <string_view>

#include "config/paths.hpp"

namespace termusic::streaming {
namespace {

std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

std::string quote(std::string_view value) {
  std::string result = "\"";
  for (const char character : value) {
    switch (character) {
    case '\\':
    case '"':
      result.push_back('\\');
      result.push_back(character);
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result.push_back(character);
      break;
    }
  }
  result.push_back('"');
  return result;
}

std::string unquote(std::string_view input) {
  const std::string value = trim(input);
  if (value.size() < 2 || value.front() != '"' || value.back() != '"')
    return value;
  std::string result;
  for (std::size_t index = 1; index + 1 < value.size(); ++index) {
    if (value[index] != '\\' || index + 2 >= value.size()) {
      result.push_back(value[index]);
      continue;
    }
    switch (value[++index]) {
    case 'n':
      result.push_back('\n');
      break;
    case 'r':
      result.push_back('\r');
      break;
    case 't':
      result.push_back('\t');
      break;
    default:
      result.push_back(value[index]);
      break;
    }
  }
  return result;
}

void appendIfValid(std::vector<Track> &tracks, Track track) {
  if (!track.valid())
    return;
  const auto duplicate =
      std::find_if(tracks.begin(), tracks.end(), [&](const Track &candidate) {
        return candidate.provider_id == track.provider_id &&
               candidate.track_id == track.track_id;
      });
  if (duplicate == tracks.end())
    tracks.push_back(std::move(track));
}

} // namespace

StreamStore::StreamStore(std::filesystem::path path)
    : path_(path.empty() ? defaultPath() : std::move(path)) {}

std::filesystem::path StreamStore::defaultPath() {
  return resolveAppPaths().streamsFile();
}

bool StreamStore::load(std::string *warning) {
  tracks_.clear();
  ++revision_;
  std::ifstream input(path_);
  if (!input) {
    std::error_code code;
    if (!path_.empty() && std::filesystem::exists(path_, code) &&
        warning != nullptr)
      *warning = "Cannot read " + path_.string() + "; Streams starts empty";
    return false;
  }

  Track current;
  bool in_entry = false;
  std::string line;
  while (std::getline(input, line)) {
    const std::string text = trim(line);
    if (text.empty() || text.front() == '#')
      continue;
    if (text == "[[stream]]") {
      if (in_entry)
        appendIfValid(tracks_, std::move(current));
      current = Track{};
      in_entry = true;
      continue;
    }
    if (!in_entry)
      continue;
    const auto equals = text.find('=');
    if (equals == std::string::npos)
      continue;
    const std::string key = trim(text.substr(0, equals));
    const std::string value = unquote(text.substr(equals + 1));
    if (key == "provider")
      current.provider_id = value;
    else if (key == "id")
      current.track_id = value;
    else if (key == "title")
      current.title = value;
    else if (key == "artist")
      current.artist = value;
    else if (key == "album")
      current.album = value;
    else if (key == "artwork")
      current.artwork_uri = value;
    else if (key == "duration") {
      try {
        current.duration_seconds = std::stod(value);
      } catch (const std::exception &) {
        current.duration_seconds = 0.0;
      }
    } else if (key == "live")
      current.live = value == "true";
  }
  if (in_entry)
    appendIfValid(tracks_, std::move(current));
  return true;
}

bool StreamStore::save(std::string *error) const {
  if (path_.empty()) {
    if (error != nullptr)
      *error = "Cannot determine the streaming data path";
    return false;
  }
  std::error_code code;
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), code);
    if (code) {
      if (error != nullptr)
        *error = "Cannot create " + path_.parent_path().string() + ": " +
                 code.message();
      return false;
    }
  }

  std::filesystem::path temporary = path_;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      if (error != nullptr)
        *error = "Cannot write " + temporary.string();
      return false;
    }
    output << "# termusic streaming sources\n";
    for (const Track &track : tracks_) {
      output << "[[stream]]\n"
             << "provider = " << quote(track.provider_id) << '\n'
             << "id = " << quote(track.track_id) << '\n'
             << "title = " << quote(track.title) << '\n'
             << "artist = " << quote(track.artist) << '\n'
             << "album = " << quote(track.album) << '\n'
             << "artwork = " << quote(track.artwork_uri) << '\n'
             << "duration = " << track.duration_seconds << '\n'
             << "live = " << (track.live ? "true" : "false") << "\n\n";
    }
    output.flush();
    if (!output) {
      if (error != nullptr)
        *error = "Cannot write " + temporary.string();
      return false;
    }
  }
  std::filesystem::rename(temporary, path_, code);
  if (code) {
    std::filesystem::remove(temporary, code);
    if (error != nullptr)
      *error = "Cannot replace " + path_.string();
    return false;
  }
  return true;
}

bool StreamStore::add(Track track) {
  if (!track.valid())
    return false;
  const std::size_t before = tracks_.size();
  appendIfValid(tracks_, std::move(track));
  if (tracks_.size() == before)
    return false;
  ++revision_;
  return true;
}

int StreamStore::removePositions(const std::vector<std::size_t> &positions) {
  std::set<std::size_t, std::greater<>> unique;
  for (const std::size_t position : positions) {
    if (position < tracks_.size())
      unique.insert(position);
  }
  for (const std::size_t position : unique)
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(position));
  if (!unique.empty())
    ++revision_;
  return static_cast<int>(unique.size());
}

} // namespace termusic::streaming
