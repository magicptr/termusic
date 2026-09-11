#include "app/diagnostics.hpp"

#include <cctype>
#include <ctime>
#include <fstream>
#include <system_error>

namespace termusic {
namespace {

std::string timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0)
    return "?";
  return buffer;
}

/// One entry per line: a message that spans lines would make the log
/// unreadable and could forge entries.
std::string singleLine(const std::string &message) {
  std::string result;
  result.reserve(message.size());
  for (const char character : message) {
    result.push_back(character == '\n' || character == '\r' ? ' ' : character);
  }
  return result;
}

bool isKeyCharacter(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
         character == '_' || character == '-';
}

} // namespace

std::string DiagnosticsLog::redact(const std::string &message) {
  // Two shapes can carry a secret:
  //   password = "hunter2"      anything keyed `password`
  //   MPD_HOST = "hunter2@host" the libmpdclient convention, where the part
  //                             before '@' is the password
  // The KEY is kept so the line still says what went wrong, and a bare host
  // name is left alone because it is exactly what makes a log useful.
  std::string result = message;
  std::string lowered;
  lowered.reserve(message.size());
  for (const char character : message)
    lowered.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));

  const auto mask = [&result](std::size_t first, std::size_t last,
                              const std::string &replacement) {
    result.replace(first, last - first, replacement);
  };

  for (const char *needle : {"password", "mpd_host"}) {
    const std::string key(needle);
    std::size_t search = 0;
    while (true) {
      const std::size_t found = lowered.find(key, search);
      if (found == std::string::npos)
        break;
      std::size_t cursor = found + key.size();
      if (cursor < lowered.size() && isKeyCharacter(lowered[cursor])) {
        search = cursor;
        continue;
      }
      while (cursor < result.size() &&
             std::isspace(static_cast<unsigned char>(result[cursor])) != 0)
        ++cursor;
      if (cursor < result.size() && (result[cursor] == '=' ||
                                     result[cursor] == ':' ||
                                     result[cursor] == '@'))
        ++cursor;
      while (cursor < result.size() &&
             std::isspace(static_cast<unsigned char>(result[cursor])) != 0)
        ++cursor;

      std::size_t value_first = cursor;
      std::size_t value_last = cursor;
      bool quoted = false;
      char quote_character = 0;
      if (value_last < result.size() &&
          (result[value_last] == '"' || result[value_last] == '\'')) {
        quoted = true;
        quote_character = result[value_last];
        ++value_first;
        value_last = value_first;
        while (value_last < result.size() &&
               result[value_last] != quote_character)
          ++value_last;
      } else {
        while (value_last < result.size() &&
               std::isspace(static_cast<unsigned char>(result[value_last])) == 0)
          ++value_last;
      }
      if (value_last <= value_first) {
        search = cursor;
        continue;
      }

      const std::string value = result.substr(value_first, value_last - value_first);
      const std::size_t at = value.find('@');
      if (at != std::string::npos && at > 0) {
        // "secret@host": hide the secret, keep the host.
        mask(value_first, value_first + at, "<hidden>");
        const std::string replaced = "<hidden>" + value.substr(at);
        lowered.replace(value_first, value_last - value_first, replaced);
        search = value_first + replaced.size();
      } else if (key == "password") {
        mask(value_first, value_last, "<hidden>");
        lowered.replace(value_first, value_last - value_first, "<hidden>");
        search = value_first + std::string("<hidden>").size();
      } else {
        search = value_last;
      }
      (void)quoted;
    }
  }
  return result;
}

DiagnosticsLog::DiagnosticsLog(std::filesystem::path path)
    : path_(std::move(path)) {}

void DiagnosticsLog::write(const std::string &message) const {
  if (path_.empty() || message.empty())
    return;
  std::error_code error;
  if (!path_.parent_path().empty())
    std::filesystem::create_directories(path_.parent_path(), error);
  if (error)
    return;
  // A log that grows without bound is a bug of its own; when it is full it is
  // replaced, not rotated forever.
  const auto size = std::filesystem::file_size(path_, error);
  if (!error && size > kMaxBytes) {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  } else {
    error.clear();
  }
  std::ofstream output(path_, std::ios::app);
  if (!output)
    return;
  output << "[" << timestamp() << "] " << singleLine(redact(message)) << '\n';
}

} // namespace termusic
