#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace termusic {

/// A small append-only log of things that went wrong.
///
/// It exists because a failure that scrolls past in a toast is impossible to
/// report afterwards: "it said something about the connection" is not a bug
/// report. The file lives in termusic's XDG cache directory, is created only
/// when there is something to write, and is truncated when it grows past a
/// small cap so it can never become a disk-space problem.
///
/// SECRETS: callers must never pass a password. `write()` additionally masks the
/// value of anything that looks like a `password = ...` assignment, because a
/// message assembled from a configuration file is exactly where a secret could
/// slip in by accident.
class DiagnosticsLog {
public:
  /// An empty path disables the log entirely (used by the tests and by any
  /// environment without a cache directory).
  explicit DiagnosticsLog(std::filesystem::path path = {});

  bool enabled() const { return !path_.empty(); }
  const std::filesystem::path &path() const { return path_; }

  /// Never throws and never fails loudly: diagnostics must not be able to break
  /// the program they are describing.
  void write(const std::string &message) const;

  /// The masking rule, exposed so a test can pin it.
  static std::string redact(const std::string &message);

  /// Files larger than this are replaced rather than appended to.
  static constexpr std::uintmax_t kMaxBytes = 64U * 1024U;

private:
  std::filesystem::path path_;
};

} // namespace termusic
