#pragma once

#include <string_view>

#include "streaming/provider.hpp"

namespace termusic::streaming {

/// Built-in baseline provider for internet radio and direct HTTP(S) audio.
/// The URL itself is stable identity and needs no network dependency in core.
class DirectUrlProvider final : public Provider {
public:
  static constexpr std::string_view kId = "direct-url";

  ProviderInfo info() const override;
  Result<ResolvedStream> resolve(const Track &track,
                                 std::stop_token stop) override;

  static Result<Track> makeTrack(std::string url, std::string title = {});
  static bool supports(std::string_view url);
};

} // namespace termusic::streaming
