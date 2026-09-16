#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "streaming/types.hpp"

namespace termusic::streaming {

/// Durable, application-owned list of streams selected by the user. Entries
/// keep provider identity and metadata, never a provider's expiring resolved
/// URL. The built-in direct-url provider happens to use the URL as track_id.
class StreamStore {
public:
  explicit StreamStore(std::filesystem::path path = {});

  static std::filesystem::path defaultPath();

  bool load(std::string *warning = nullptr);
  bool save(std::string *error = nullptr) const;

  bool add(Track track);
  int removePositions(const std::vector<std::size_t> &positions);

  const std::vector<Track> &tracks() const { return tracks_; }
  std::size_t revision() const { return revision_; }
  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
  std::vector<Track> tracks_;
  std::size_t revision_ = 0;
};

} // namespace termusic::streaming
