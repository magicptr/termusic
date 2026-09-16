#include "streaming/direct_url_provider.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace termusic::streaming {

ProviderInfo DirectUrlProvider::info() const {
  return {std::string(kId), "Direct URL", Capability::Resolve};
}

bool DirectUrlProvider::supports(std::string_view url) {
  const bool http = url.starts_with("http://") || url.starts_with("https://");
  if (!http)
    return false;
  return std::none_of(url.begin(), url.end(), [](unsigned char character) {
    return std::isspace(character) != 0 || std::iscntrl(character) != 0;
  });
}

Result<Track> DirectUrlProvider::makeTrack(std::string url, std::string title) {
  if (!supports(url)) {
    return Result<Track>::failure(
        {ErrorCode::InvalidRequest,
         "A direct stream must be an HTTP or HTTPS URL without whitespace",
         {}});
  }
  Track track;
  track.provider_id = kId;
  track.track_id = std::move(url);
  track.title = title.empty() ? track.track_id : std::move(title);
  track.live = true;
  return Result<Track>::success(std::move(track));
}

Result<ResolvedStream> DirectUrlProvider::resolve(const Track &track,
                                                  std::stop_token stop) {
  if (stop.stop_requested()) {
    return Result<ResolvedStream>::failure(
        {ErrorCode::Cancelled, "Stream resolution was cancelled", {}});
  }
  if (track.provider_id != kId || !supports(track.track_id)) {
    return Result<ResolvedStream>::failure(
        {ErrorCode::InvalidRequest, "Invalid direct stream track", {}});
  }
  return Result<ResolvedStream>::success({track.track_id, {}});
}

} // namespace termusic::streaming
