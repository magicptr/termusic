#pragma once

#include <string>
#include <string_view>

namespace termusic::util {

/// Terminal display width of a UTF-8 string (CJK glyphs count as 2 columns).
int displayWidth(std::string_view text);

/// Truncate `text` so it occupies at most `max_width` columns, appending "…"
/// when something was cut. Never splits a UTF-8 glyph.
std::string ellipsize(std::string_view text, int max_width);

/// Truncate `text` to at most `max_width` columns WITHOUT an ellipsis. Used
/// when a row has to be clipped to the panel it is drawn in: FTXUI compresses
/// every child of an over-wide box, so a multi-column row must be cut to the
/// available width instead of being handed a box it cannot fill.
std::string clipToWidth(std::string_view text, int max_width);

/// Pad on the right with spaces until `width` columns are used.
std::string padRight(std::string_view text, int width);

/// Pad on the left with spaces until `width` columns are used.
std::string padLeft(std::string_view text, int width);

/// "3:07", "1:02:33", or "--:--" when the value is unusable.
std::string formatDuration(double seconds);

/// Binary byte size as a human-readable string: "45.2 MB", "1.4 GB", "872 B".
/// Binary units with the conventional spelling, so a file the file manager
/// calls "45.2 MB" is not shown here as "47.4 MB". A non-positive or
/// non-finite value means "unknown" and renders as "--".
std::string formatFileSize(double bytes);

/// Epoch seconds as the full local date and time a track was played:
/// "2026-02-14 09:31:05". A timestamp of 0 -- an entry recorded before the
/// history file carried timestamps -- renders as "--" rather than 1970.
std::string formatPlayedAt(long long epoch_seconds);

/// Colourful label used by the player bar, e.g. "75%".
std::string formatVolume(int volume);

/// Text after the last '/' of a URI, i.e. the file name.
std::string basename(std::string_view uri);

/// Lower-cased copy, used for case-insensitive filtering.
std::string toLower(std::string_view text);

/// True when `haystack` contains `needle`, ignoring ASCII case.
bool containsIgnoreCase(std::string_view haystack, std::string_view needle);

/// `glyph` repeated `count` times; negative counts yield an empty string.
std::string repeat(int count, std::string_view glyph = " ");

} // namespace termusic::util
