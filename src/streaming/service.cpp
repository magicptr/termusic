#include "streaming/service.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_set>

namespace termusic::streaming {

bool Service::registerProvider(std::shared_ptr<Provider> provider,
                               std::string *error) {
  if (provider == nullptr) {
    if (error != nullptr)
      *error = "Cannot register an empty streaming provider";
    return false;
  }
  const ProviderInfo candidate = provider->info();
  if (candidate.id.empty() || candidate.name.empty()) {
    if (error != nullptr)
      *error = "A streaming provider needs a stable id and display name";
    return false;
  }

  std::unique_lock lock(mutex_);
  const auto duplicate = std::find_if(
      providers_.begin(), providers_.end(), [&](const auto &registered) {
        return registered->info().id == candidate.id;
      });
  if (duplicate != providers_.end()) {
    if (error != nullptr)
      *error =
          "Streaming provider '" + candidate.id + "' is already registered";
    return false;
  }
  providers_.push_back(std::move(provider));
  return true;
}

bool Service::unregisterProvider(std::string_view id) {
  std::unique_lock lock(mutex_);
  const auto old_size = providers_.size();
  providers_.erase(std::remove_if(providers_.begin(), providers_.end(),
                                  [&](const auto &candidate) {
                                    return candidate->info().id == id;
                                  }),
                   providers_.end());
  return providers_.size() != old_size;
}

std::vector<ProviderInfo> Service::providers() const {
  std::shared_lock lock(mutex_);
  std::vector<ProviderInfo> result;
  result.reserve(providers_.size());
  for (const auto &provider : providers_)
    result.push_back(provider->info());
  return result;
}

std::shared_ptr<Provider> Service::provider(std::string_view id) const {
  std::shared_lock lock(mutex_);
  const auto found = std::find_if(
      providers_.begin(), providers_.end(),
      [&](const auto &candidate) { return candidate->info().id == id; });
  return found == providers_.end() ? nullptr : *found;
}

Result<SearchPage> Service::search(std::string_view provider_id,
                                   const SearchRequest &request,
                                   std::stop_token stop) const {
  if (request.limit == 0) {
    return Result<SearchPage>::failure(
        {ErrorCode::InvalidRequest,
         "Search limit must be greater than zero",
         {}});
  }
  const auto source = provider(provider_id);
  if (source == nullptr) {
    return Result<SearchPage>::failure({ErrorCode::ProviderNotFound,
                                        "Streaming provider '" +
                                            std::string(provider_id) +
                                            "' is not registered",
                                        {}});
  }
  if (stop.stop_requested()) {
    return Result<SearchPage>::failure(
        {ErrorCode::Cancelled, "Streaming search was cancelled", {}});
  }
  if (!hasCapability(source->info().capabilities, Capability::Search)) {
    return Result<SearchPage>::failure({ErrorCode::Unsupported,
                                        "Streaming provider '" +
                                            std::string(provider_id) +
                                            "' does not support search",
                                        {}});
  }
  return source->search(request, stop);
}

CatalogSearchResult Service::searchAll(const SearchRequest &request,
                                       std::stop_token stop) const {
  std::vector<std::shared_ptr<Provider>> snapshot;
  {
    std::shared_lock lock(mutex_);
    snapshot = providers_;
  }

  CatalogSearchResult result;
  std::unordered_set<std::string> identities;
  for (const auto &source : snapshot) {
    const ProviderInfo provider_info = source->info();
    if (!hasCapability(provider_info.capabilities, Capability::Search))
      continue;
    if (stop.stop_requested()) {
      result.failures.push_back(
          {provider_info.id,
           {ErrorCode::Cancelled, "Streaming search was cancelled", {}}});
      break;
    }
    const auto page = search(provider_info.id, request, stop);
    if (!page) {
      result.failures.push_back({provider_info.id, page.error()});
      continue;
    }
    for (const Track &track : page.value().tracks) {
      if (!track.valid() || track.provider_id != provider_info.id)
        continue;
      if (identities.insert(track.stableId()).second)
        result.tracks.push_back(track);
    }
  }
  return result;
}

Result<ResolvedStream> Service::resolve(const Track &track,
                                        std::stop_token stop) const {
  if (!track.valid()) {
    return Result<ResolvedStream>::failure(
        {ErrorCode::InvalidRequest,
         "A streaming track needs a provider id and track id",
         {}});
  }
  const auto source = provider(track.provider_id);
  if (source == nullptr) {
    return Result<ResolvedStream>::failure(
        {ErrorCode::ProviderNotFound,
         "Streaming provider '" + track.provider_id + "' is not registered",
         {}});
  }
  if (stop.stop_requested()) {
    return Result<ResolvedStream>::failure(
        {ErrorCode::Cancelled, "Stream resolution was cancelled", {}});
  }
  if (!hasCapability(source->info().capabilities, Capability::Resolve)) {
    return Result<ResolvedStream>::failure(
        {ErrorCode::Unsupported,
         "Streaming provider '" + track.provider_id +
             "' cannot resolve playback URLs",
         {}});
  }
  auto resolved = source->resolve(track, stop);
  if (resolved && resolved.value().uri.empty()) {
    return Result<ResolvedStream>::failure(
        {ErrorCode::ProviderFailure,
         "Streaming provider returned an empty playback URL",
         {}});
  }
  return resolved;
}

Song Service::playableSong(const Track &track, const ResolvedStream &stream) {
  Song song;
  song.uri = stream.uri;
  song.source_id = track.provider_id;
  song.source_track_id = track.track_id;
  song.title = track.title;
  song.artist = track.artist;
  song.album = track.album;
  song.duration_seconds = track.duration_seconds;
  song.is_live_stream = track.live;
  return song;
}

} // namespace termusic::streaming
