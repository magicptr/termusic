#include <cassert>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

#include "streaming/direct_url_provider.hpp"
#include "streaming/service.hpp"
#include "streaming/stream_store.hpp"
#include "streaming/subsonic_provider.hpp"

namespace {

class FakeHttpClient final : public termusic::streaming::HttpClient {
public:
  termusic::streaming::HttpResponse
  get(std::string_view url, int, std::stop_token) const override {
    last_url = std::string(url);
    return response;
  }

  mutable std::string last_url;
  termusic::streaming::HttpResponse response;
};

class SearchProvider final : public termusic::streaming::Provider {
public:
  SearchProvider(std::string id, bool fail = false)
      : id_(std::move(id)), fail_(fail) {}

  termusic::streaming::ProviderInfo info() const override {
    using termusic::streaming::Capability;
    return {id_, id_, Capability::Search | Capability::Resolve};
  }

  termusic::streaming::Result<termusic::streaming::SearchPage>
  search(const termusic::streaming::SearchRequest &request,
         std::stop_token stop) override {
    using namespace termusic::streaming;
    if (stop.stop_requested())
      return Result<SearchPage>::failure(
          {ErrorCode::Cancelled, "cancelled", {}});
    if (fail_)
      return Result<SearchPage>::failure({ErrorCode::Network, "offline", {}});
    Track track;
    track.provider_id = id_;
    track.track_id = request.query;
    track.title = "Result from " + id_;
    return Result<SearchPage>::success({{std::move(track)}, {}});
  }

  termusic::streaming::Result<termusic::streaming::ResolvedStream>
  resolve(const termusic::streaming::Track &track, std::stop_token) override {
    using namespace termusic::streaming;
    return Result<ResolvedStream>::success(
        {"https://stream.example/" + track.track_id, {}});
  }

private:
  std::string id_;
  bool fail_ = false;
};

} // namespace

int main() {
  using namespace termusic;
  using namespace termusic::streaming;

  Service service;
  std::string error;
  assert(!service.registerProvider(nullptr, &error));
  assert(!error.empty());

  auto alpha = std::make_shared<SearchProvider>("alpha");
  auto offline = std::make_shared<SearchProvider>("offline", true);
  assert(service.registerProvider(alpha, &error));
  assert(service.registerProvider(offline, &error));
  assert(!service.registerProvider(std::make_shared<SearchProvider>("alpha"),
                                   &error));
  assert(service.providers().size() == 2);

  // Subsonic/Navidrome: salted-token authentication, search3 XML parsing and
  // a fresh authenticated stream URL. No network is used.
  assert(SubsonicProvider::md5Hex("") ==
         "d41d8cd98f00b204e9800998ecf8427e");
  assert(SubsonicProvider::md5Hex("sesamec19b2d") ==
         "26719a1196d2a940705a59634eb18eab");
  auto http = std::make_shared<FakeHttpClient>();
  http->response.status = 200;
  http->response.body =
      R"XML(<?xml version="1.0"?><subsonic-response status="ok" version="1.16.1"><searchResult3><song id="song-1" title="Rock &amp; Roll" artist="Alice" album="Live" duration="213" coverArt="cover-1"/></searchResult3></subsonic-response>)XML";
  SubsonicSettings subsonic_settings{"https://music.example.com/", "joe",
                                       "sesame", 2000};
  auto subsonic = std::make_shared<SubsonicProvider>(
      subsonic_settings, http, [] { return std::string("c19b2d"); });
  SearchRequest subsonic_request;
  subsonic_request.query = "rock & roll";
  subsonic_request.limit = 12;
  const auto subsonic_results = subsonic->search(subsonic_request, {});
  assert(subsonic_results);
  assert(subsonic_results.value().tracks.size() == 1);
  const Track &subsonic_track = subsonic_results.value().tracks.front();
  assert(subsonic_track.track_id == "song-1");
  assert(subsonic_track.title == "Rock & Roll");
  assert(subsonic_track.duration_seconds == 213.0);
  assert(http->last_url.find("/rest/search3.view?") != std::string::npos);
  assert(http->last_url.find("t=26719a1196d2a940705a59634eb18eab") !=
         std::string::npos);
  assert(http->last_url.find("query=rock%20%26%20roll") != std::string::npos);
  const auto subsonic_stream = subsonic->resolve(subsonic_track, {});
  assert(subsonic_stream);
  assert(subsonic_stream.value().uri.find("/rest/stream.view?") !=
         std::string::npos);
  assert(subsonic_stream.value().uri.find("id=song-1") != std::string::npos);

  http->response.body =
      R"XML(<subsonic-response status="failed" version="1.16.1"><error code="40" message="Wrong username or password"/></subsonic-response>)XML";
  const auto rejected = subsonic->search(subsonic_request, {});
  assert(!rejected);
  assert(rejected.error().code == ErrorCode::Authentication);

  SearchRequest request;
  request.query = "night-drive";
  request.limit = 10;
  const auto one = service.search("alpha", request);
  assert(one);
  assert(one.value().tracks.size() == 1);
  assert(one.value().tracks.front().stableId() == "alpha:night-drive");

  // Catalog search retains healthy results when another provider is offline.
  const CatalogSearchResult all = service.searchAll(request);
  assert(all.tracks.size() == 1);
  assert(all.failures.size() == 1);
  assert(all.failures.front().provider_id == "offline");
  assert(all.failures.front().error.code == ErrorCode::Network);

  const auto resolved = service.resolve(all.tracks.front());
  assert(resolved);
  assert(resolved.value().uri == "https://stream.example/night-drive");
  const Song playable =
      Service::playableSong(all.tracks.front(), resolved.value());
  assert(playable.uri == resolved.value().uri);
  assert(playable.source_id == "alpha");
  assert(playable.source_track_id == "night-drive");

  SearchRequest invalid;
  invalid.limit = 0;
  assert(service.search("alpha", invalid).error().code ==
         ErrorCode::InvalidRequest);
  assert(service.search("missing", request).error().code ==
         ErrorCode::ProviderNotFound);

  std::stop_source cancellation;
  cancellation.request_stop();
  assert(
      service.search("alpha", request, cancellation.get_token()).error().code ==
      ErrorCode::Cancelled);

  auto direct = std::make_shared<DirectUrlProvider>();
  assert(service.registerProvider(direct));
  assert(DirectUrlProvider::supports("https://radio.example/live.mp3"));
  assert(!DirectUrlProvider::supports("file:///tmp/song.mp3"));
  assert(!DirectUrlProvider::supports("https://radio.example/bad url"));
  const auto radio = DirectUrlProvider::makeTrack(
      "https://radio.example/live.mp3", "Example Radio");
  assert(radio);
  assert(radio.value().live);
  const auto radio_stream = service.resolve(radio.value());
  assert(radio_stream);
  assert(radio_stream.value().uri == "https://radio.example/live.mp3");
  const Song radio_song =
      Service::playableSong(radio.value(), radio_stream.value());
  assert(radio_song.is_live_stream);

  // A resolve-only source is not treated as a searchable Agent catalog.
  assert(service.search("direct-url", request).error().code ==
         ErrorCode::Unsupported);
  assert(service.unregisterProvider("offline"));
  assert(!service.unregisterProvider("offline"));
  assert(service.providers().size() == 2);

  const std::filesystem::path store_path =
      std::filesystem::temp_directory_path() / "termusic_streams_test.toml";
  std::filesystem::remove(store_path);
  StreamStore store(store_path);
  assert(store.add(radio.value()));
  assert(!store.add(radio.value())); // stable identity is unique
  assert(store.save(&error));
  StreamStore reloaded(store_path);
  assert(reloaded.load(&error));
  assert(reloaded.tracks().size() == 1);
  assert(reloaded.tracks().front().stableId() == radio.value().stableId());
  assert(reloaded.tracks().front().title == "Example Radio");
  assert(reloaded.tracks().front().live);
  assert(reloaded.removePositions({0, 0, 99}) == 1);
  assert(reloaded.tracks().empty());
  assert(reloaded.save(&error));
  std::filesystem::remove(store_path);
}
