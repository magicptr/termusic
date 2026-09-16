#pragma once

#include <stop_token>

#include "streaming/types.hpp"

namespace termusic::streaming {

/// Implemented inside termusic for every supported service. Providers own
/// authentication, pagination and service-specific payloads; callers only see
/// stable tracks and resolved MPD-compatible URIs.
class Provider {
public:
  virtual ~Provider() = default;

  virtual ProviderInfo info() const = 0;

  virtual Result<SearchPage> search(const SearchRequest &request,
                                    std::stop_token stop);
  virtual Result<ResolvedStream> resolve(const Track &track,
                                         std::stop_token stop);
};

} // namespace termusic::streaming
