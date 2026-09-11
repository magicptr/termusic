#include "util/text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iterator>

#include <ftxui/screen/string.hpp>

namespace termusic::util {
namespace {

// A single UTF-8 glyph plus the number of bytes it consumed.
struct Glyph {
  std::string text;
  int width = 0;
  std::size_t bytes = 0;
};

Glyph nextGlyph(std::string_view input, std::size_t offset) {
  Glyph glyph;
  if (offset >= input.size()) {
    return glyph;
  }

  const auto lead = static_cast<unsigned char>(input[offset]);
  std::size_t length = 1;
  if ((lead & 0x80U) == 0x00U) {
    length = 1;
  } else if ((lead & 0xE0U) == 0xC0U) {
    length = 2;
  } else if ((lead & 0xF0U) == 0xE0U) {
    length = 3;
  } else if ((lead & 0xF8U) == 0xF0U) {
    length = 4;
  }

  // Guard against truncated sequences at the end of the string.
  length = std::min(length, input.size() - offset);
  glyph.text = std::string(input.substr(offset, length));
  glyph.bytes = length;
  glyph.width = std::max(0, ftxui::string_width(glyph.text));
  return glyph;
}

} // namespace

int displayWidth(std::string_view text) {
  if (text.empty()) {
    return 0;
  }
  return std::max(0, ftxui::string_width(text));
}

std::string ellipsize(std::string_view text, int max_width) {
  if (max_width <= 0) {
    return {};
  }
  if (displayWidth(text) <= max_width) {
    return std::string(text);
  }

  constexpr std::string_view kEllipsis = "…";
  const int ellipsis_width = displayWidth(kEllipsis);
  const int budget = max_width - ellipsis_width;
  if (budget <= 0) {
    // Not even room for the ellipsis: show nothing rather than overflowing.
    return {};
  }

  std::string result;
  int used = 0;
  for (std::size_t offset = 0; offset < text.size();) {
    const Glyph glyph = nextGlyph(text, offset);
    if (glyph.bytes == 0) {
      break;
    }
    if (used + glyph.width > budget) {
      break;
    }
    result += glyph.text;
    used += glyph.width;
    offset += glyph.bytes;
  }
  result += kEllipsis;
  return result;
}

std::string clipToWidth(std::string_view text, int max_width) {
  if (max_width <= 0) {
    return {};
  }
  if (displayWidth(text) <= max_width) {
    return std::string(text);
  }
  std::string result;
  int used = 0;
  for (std::size_t offset = 0; offset < text.size();) {
    const Glyph glyph = nextGlyph(text, offset);
    if (glyph.bytes == 0) {
      break;
    }
    if (used + glyph.width > max_width) {
      break;
    }
    result += glyph.text;
    used += glyph.width;
    offset += glyph.bytes;
  }
  return result;
}

std::string padRight(std::string_view text, int width) {
  std::string result(text);
  const int missing = width - displayWidth(result);
  if (missing > 0) {
    result.append(static_cast<std::size_t>(missing), ' ');
  }
  return result;
}

std::string padLeft(std::string_view text, int width) {
  const int missing = width - displayWidth(text);
  if (missing <= 0) {
    return std::string(text);
  }
  return std::string(static_cast<std::size_t>(missing), ' ') +
         std::string(text);
}

std::string formatDuration(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0.0) {
    return "--:--";
  }

  const auto total = static_cast<long long>(seconds + 0.5);
  const long long hours = total / 3600;
  const long long minutes = (total % 3600) / 60;
  const long long secs = total % 60;

  char buffer[32];
  if (hours > 0) {
    std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", hours, minutes,
                  secs);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", minutes, secs);
  }
  return std::string(buffer);
}

std::string formatFileSize(double bytes) {
  if (!std::isfinite(bytes) || bytes <= 0.0) {
    return "--";
  }
  // Binary multiples (KiB = 1024). One decimal is enough to tell 4.2 MB from
  // 4.3 MB, and it keeps the widest value ("1023.9 MB") inside the nine cells
  // the Library's Size column budgets for it.
  static constexpr const char *kUnits[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  std::size_t unit = 0;
  double value = bytes;
  while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[32];
  if (unit == 0) {
    std::snprintf(buffer, sizeof(buffer), "%.0f %s", value, kUnits[unit]);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
  }
  return std::string(buffer);
}

std::string formatPlayedAt(long long epoch_seconds) {
  if (epoch_seconds <= 0) {
    return "--";
  }
  const std::time_t stamp = static_cast<std::time_t>(epoch_seconds);
  std::tm local{};
#if defined(_WIN32)
  if (localtime_s(&local, &stamp) != 0) {
    return "--";
  }
#else
  if (localtime_r(&stamp, &local) == nullptr) {
    return "--";
  }
#endif
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0) {
    return "--";
  }
  return std::string(buffer);
}

std::string formatVolume(int volume) {
  if (volume < 0) {
    return "--%";
  }
  return std::to_string(volume) + "%";
}

std::string basename(std::string_view uri) {
  const auto slash = uri.find_last_of('/');
  if (slash == std::string_view::npos) {
    return std::string(uri);
  }
  return std::string(uri.substr(slash + 1));
}

std::string toLower(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return result;
}

bool containsIgnoreCase(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

std::string repeat(int count, std::string_view glyph) {
  std::string result;
  if (count <= 0 || glyph.empty()) {
    return result;
  }
  result.reserve(static_cast<std::size_t>(count) * glyph.size());
  for (int index = 0; index < count; ++index) {
    result += glyph;
  }
  return result;
}

} // namespace termusic::util
