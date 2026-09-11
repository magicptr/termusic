#pragma once

// Phase A of the Vim-style workspace redesign: the state model and the
// key-chord engine. Nothing here renders or touches audio -- it is the
// foundation the Library workspace is built on.

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace termusic {

/// Top-level application sections. Deliberately only three: Now Playing,
/// Lyrics, Search and so on are contextual views, not sections.
enum class AppSection {
  Library,
  Settings,
  Files,
};

/// Presentation is orthogonal to the section. Any section can hand the main
/// region to the immersive now-playing display and get it back untouched:
/// the section underneath is never changed, and entering or leaving the mode
/// must not reload data or reset cursors.
enum class PresentationMode {
  Normal,
  ImmersiveNowPlaying,
};

/// Vim modes. Insert exists only where text is actually being edited.
enum class VimMode {
  Normal,
  Visual,
  Insert,
  Command,
};

/// Which of the two Library panes owns the keyboard.
enum class WorkspacePane {
  Tree,
  TrackList,
};

/// The small set of key sequences that behave as prefixes. Everything else is
/// treated as a single-key token.
class ChordEngine {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;

  explicit ChordEngine(std::chrono::milliseconds timeout =
                           std::chrono::milliseconds(800))
      : timeout_(timeout) {
    // Multi-key commands from the design: gg/dd/yy, the g-prefixed view jumps,
    // and the Ctrl+w pane chords.
    for (const char *sequence :
         {"g g", "g l", "g n", "g f", "g s", "d d", "y y", "Ctrl+w h",
          "Ctrl+w l", "Ctrl+w w"}) {
      commands_.emplace_back(sequence);
    }
  }

  /// Feed one key token ("j", "g", "Ctrl+w"). Returns the completed command
  /// when the buffer resolves, otherwise nullopt while a chord is pending.
  /// A single key that cannot start any chord is returned as itself so plain
  /// navigation keeps working with no delay.
  std::optional<std::string> feed(std::string_view key,
                                  TimePoint now = Clock::now()) {
    expire(now);
    last_feed_ = now;

    if (pending_.empty()) {
      if (!starts_any_chord(key)) {
        pending_.clear();
        return std::string(key);
      }
      pending_.assign(key);
      // A one-key command that is also a prefix ("g" starts "g g") stays
      // pending; the caller sees the result on the next key or on expiry.
      return std::nullopt;
    }

    std::string candidate = pending_ + " " + std::string(key);
    if (is_command(candidate)) {
      pending_.clear();
      return candidate;
    }
    if (starts_any_chord(candidate)) {
      pending_.assign(candidate);
      return std::nullopt;
    }
    // Dead end: drop the chord and re-try this key on its own.
    pending_.clear();
    return feed(key, now);
  }

  /// Drop an incomplete chord. Called on Esc and by the UI ticker.
  void reset() { pending_.clear(); }

  /// Expire a chord that has been waiting longer than the timeout.
  void expire(TimePoint now) {
    if (!pending_.empty() && now - last_feed_ > timeout_)
      pending_.clear();
  }

  bool pending() const { return !pending_.empty(); }
  const std::string &pendingKeys() const { return pending_; }

 private:
  using Clock = std::chrono::steady_clock;

  bool is_command(std::string_view sequence) const {
    return std::find(commands_.begin(), commands_.end(), sequence) !=
           commands_.end();
  }

  /// True when `sequence` is a proper prefix of some command, or a command
  /// itself that may still be extended.
  bool starts_any_chord(std::string_view sequence) const {
    for (const std::string &command : commands_) {
      if (command.size() > sequence.size() &&
          command.compare(0, sequence.size(), sequence) == 0 &&
          command[sequence.size()] == ' ') {
        return true;
      }
    }
    return false;
  }

  std::vector<std::string> commands_;
  std::string pending_;
  std::chrono::milliseconds timeout_;
  TimePoint last_feed_{};
};

} // namespace termusic
