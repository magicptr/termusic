#pragma once

#include <memory>
#include <shared_mutex>
#include <stop_token>
#include <string_view>
#include <vector>

#include "app/state.hpp"
#include "streaming/provider.hpp"

namespace termusic::streaming {

/// Application-owned provider registry and catalog boundary. Registration is
/// safe during startup or configuration reload, while searches may run on UI
/// worker threads using a stable provider snapshot.
class Service {
public:
  bool registerProvider(std::shared_ptr<Provider> provider,
                        std::string *error = nullptr);
  bool unregisterProvider(std::string_view id);

  std::vector<ProviderInfo> providers() const;
  std::shared_ptr<Provider> provider(std::string_view id) const;

  Result<SearchPage> search(std::string_view provider_id,
                            const SearchRequest &request,
                            std::stop_token stop = {}) const;
  CatalogSearchResult searchAll(const SearchRequest &request,
                                std::stop_token stop = {}) const;
  Result<ResolvedStream> resolve(const Track &track,
                                 std::stop_token stop = {}) const;

  /// The only bridge into the current playback model. The resolved URI goes to
  /// MPD, while the queue entry keeps provider identity for later controller
  /// integration and future Agent use.
  static Song playableSong(const Track &track, const ResolvedStream &stream);

private:
  mutable std::shared_mutex mutex_;
  std::vector<std::shared_ptr<Provider>> providers_;
};

} // namespace termusic::streaming
