#include "app/history.hpp"

#include "config/paths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace termusic {
namespace {

std::string quote(std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('"');
  for (const char c : value) {
    switch (c) {
    case '\\':
    case '"':
      result.push_back('\\');
      result.push_back(c);
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
      result.push_back(c);
      break;
    }
  }
  result.push_back('"');
  return result;
}

std::string trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(first, last - first + 1));
}

/// Reverses quote(); an unterminated value is returned as-is rather than
/// dropping the rest of the file.
std::string unquote(std::string_view input) {
  std::string value = trim(input);
  if (value.size() < 2 || value.front() != '"' || value.back() != '"')
    return value;
  std::string result;
  result.reserve(value.size() - 2);
  for (std::size_t i = 1; i + 1 < value.size(); ++i) {
    if (value[i] != '\\' || i + 2 >= value.size()) {
      result.push_back(value[i]);
      continue;
    }
    switch (value[++i]) {
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
      result.push_back(value[i]);
      break;
    }
  }
  return result;
}

} // namespace

HistoryStore::HistoryStore(std::filesystem::path path)
    // `path` is read twice, so it is copied rather than moved: a moved-from
    // path is empty, which would silently turn every explicit path into "use
    // the default location and the legacy fallback".
    : path_(path.empty() ? defaultPath() : path),
      legacy_fallback_(path.empty()) {}

std::filesystem::path HistoryStore::defaultPath() {
  // Application-owned data belongs in the XDG DATA directory (the same rule
  // that puts plugins there), resolved by the one paths module.
  return resolveAppPaths().historyFile();
}

/// Where history used to live, before it moved to the data directory
/// (XDG_STATE_HOME). It is READ when the new file does not exist yet, so a
/// returning user keeps their history; nothing is ever written there again, and
/// the legacy file is left on disk untouched.
std::filesystem::path legacyHistoryPath() {
  const char *state = std::getenv("XDG_STATE_HOME");
  if (state != nullptr && *state != '\0' && std::filesystem::path(state).is_absolute())
    return std::filesystem::path(state) / "history.toml";
  const char *home = std::getenv("HOME");
  if (home != nullptr && *home != '\0')
    return std::filesystem::path(home) / ".local" / "state" / "termusic" /
           "history.toml";
  return {};
}

void HistoryStore::setMaxEntries(std::size_t max_entries) {
  max_entries_ = std::max<std::size_t>(1, max_entries);
  while (entries_.size() > max_entries_)
    entries_.erase(entries_.begin());
}

bool HistoryStore::load(std::string *warning) {
  entries_.clear();
  ++revision_;
  // A reload replaces the store wholesale, so a playback session that was
  // started from the previous contents no longer describes it.
  ++epoch_;
  next_id_ = 1;
  std::error_code filesystem_error;
  std::filesystem::path source = path_;
  // Read the pre-XDG-state file once, for the default location only.
  if (legacy_fallback_ &&
      (path_.empty() || !std::filesystem::exists(source, filesystem_error))) {
    const std::filesystem::path legacy = legacyHistoryPath();
    if (!legacy.empty() && std::filesystem::exists(legacy, filesystem_error))
      source = legacy;
  }
  std::ifstream input(source);
  if (!input) {
    if (!source.empty() && std::filesystem::exists(source, filesystem_error) &&
        warning != nullptr)
      *warning = "Cannot read " + source.string() + "; history starts empty";
    return false;
  }

  HistoryEntry current;
  bool in_entry = false;
  std::string line;
  while (std::getline(input, line)) {
    const std::string text = trim(line);
    if (text.empty() || text.front() == '#')
      continue;
    if (text == "[[entry]]") {
      if (in_entry && !current.uri.empty())
        entries_.push_back(std::move(current));
      current = HistoryEntry{};
      in_entry = true;
      continue;
    }
    const auto equals = text.find('=');
    if (equals == std::string::npos)
      continue;
    const std::string key = trim(text.substr(0, equals));
    const std::string value = unquote(text.substr(equals + 1));
    if (key == "id") {
      try {
        current.id = std::stoll(value);
      } catch (const std::exception &) {
        current.id = 0;
      }
    } else if (key == "uri")
      current.uri = value;
    else if (key == "title")
      current.title = value;
    else if (key == "artist")
      current.artist = value;
    else if (key == "album")
      current.album = value;
    else if (key == "played_at") {
      // A hand-edited file may hold anything here; an unparsable value simply
      // leaves the occurrence untimed.
      try {
        current.played_at = std::stoll(value);
      } catch (const std::exception &) {
        current.played_at = 0;
      }
    }
  }
  if (in_entry && !current.uri.empty())
    entries_.push_back(std::move(current));

  // A hand-edited file may exceed the cap: keep the newest entries.
  if (entries_.size() > max_entries_)
    entries_.erase(entries_.begin(),
                   entries_.end() - static_cast<std::ptrdiff_t>(max_entries_));
  // A file written before ids existed -- or hand-edited without them -- gets
  // fresh, unique ones. The next id always stays ahead of everything stored, so
  // an id is never handed out twice across restarts.
  for (HistoryEntry &entry : entries_) {
    if (entry.id <= 0)
      entry.id = next_id_++;
    else if (entry.id >= next_id_)
      next_id_ = entry.id + 1;
  }
  return true;
}

bool HistoryStore::save(std::string *error) const {
  std::error_code code;
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), code);
    if (code && error != nullptr) {
      *error = "Cannot create " + path_.parent_path().string() + ": " +
               code.message();
      return false;
    }
  }
  // Write to a sibling and rename, so an interrupted save cannot truncate an
  // existing history file.
  std::filesystem::path temporary = path_;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
      if (error != nullptr)
        *error = "Cannot write " + temporary.string();
      return false;
    }
    output << "# termusic playback history\n"
              "# Chronological: the OLDEST occurrence is first. At most "
           << max_entries_ << " entries.\n";
    for (const HistoryEntry &entry : entries_) {
      output << "[[entry]]\n"
             << "id = " << entry.id << '\n'
             << "uri = " << quote(entry.uri) << '\n'
             << "title = " << quote(entry.title) << '\n'
             << "artist = " << quote(entry.artist) << '\n'
             << "album = " << quote(entry.album) << '\n'
             << "played_at = " << entry.played_at << '\n';
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

void HistoryStore::record(const Song &song) {
  const auto now = std::chrono::system_clock::now();
  recordAt(song, std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch())
                     .count());
}

void HistoryStore::recordAt(const Song &song, long long played_at) {
  if (song.uri.empty())
    return;
  HistoryEntry entry;
  entry.id = next_id_++;
  entry.uri = song.uri;
  entry.title = song.title;
  entry.artist = song.artist;
  entry.album = song.album;
  entry.played_at = played_at;
  entries_.push_back(std::move(entry));
  // FIFO eviction: the newest max_entries_ entries survive.
  while (entries_.size() > max_entries_)
    entries_.erase(entries_.begin());
  ++revision_;
}

std::vector<Song> HistoryStore::songsNewestFirst() const {
  std::vector<Song> songs;
  songs.reserve(entries_.size());
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    Song song;
    song.uri = it->uri;
    song.title = it->title;
    song.artist = it->artist;
    song.album = it->album;
    song.played_at_epoch = it->played_at;
    song.history_record_id = it->id;
    songs.push_back(std::move(song));
  }
  return songs;
}

std::vector<long long> HistoryStore::timesNewestFirst() const {
  std::vector<long long> times;
  times.reserve(entries_.size());
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it)
    times.push_back(it->played_at);
  return times;
}

void HistoryStore::clear() {
  entries_.clear();
  ++revision_;
  // The store is gone as a list: a session started from it can no longer claim
  // any row here.
  ++epoch_;
}

bool HistoryStore::removeRecord(long long id) {
  if (id <= 0)
    return false;
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [id](const HistoryEntry &entry) {
                                    return entry.id == id;
                                  });
  if (found == entries_.end())
    return false;
  entries_.erase(found);
  ++revision_;
  return true;
}

int HistoryStore::removeRange(std::size_t first, std::size_t count) {
  if (count == 0 || first >= entries_.size())
    return 0;
  const std::size_t last = std::min(entries_.size(), first + count);
  const auto from = entries_.begin() + static_cast<std::ptrdiff_t>(first);
  const auto to = entries_.begin() + static_cast<std::ptrdiff_t>(last);
  const int removed = static_cast<int>(std::distance(from, to));
  entries_.erase(from, to);
  ++revision_;
  return removed;
}

bool HistoryStore::hasRecord(long long id) const {
  if (id <= 0)
    return false;
  return std::any_of(entries_.begin(), entries_.end(),
                     [id](const HistoryEntry &entry) {
                       return entry.id == id;
                     });
}

long long HistoryStore::recordIdAt(std::size_t index) const {
  return index < entries_.size() ? entries_[index].id : 0;
}

} // namespace termusic
