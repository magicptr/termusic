#pragma once

#include <functional>
#include <memory>
#include <string>

#include "streaming/http_client.hpp"
#include "streaming/provider.hpp"

namespace termusic::streaming {

struct SubsonicSettings {
  std::string server_url;
  std::string username;
  std::string password;
  int timeout_ms = 8000;

  bool valid() const {
    return (server_url.starts_with("http://") ||
            server_url.starts_with("https://")) &&
           !username.empty() && !password.empty();
  }
};

class SubsonicProvider final : public Provider {
public:
  using SaltFactory = std::function<std::string()>;

  explicit SubsonicProvider(
      SubsonicSettings settings,
      std::shared_ptr<const HttpClient> http =
          std::make_shared<CurlHttpClient>(),
      SaltFactory salt_factory = {});

  ProviderInfo info() const override;
  Result<SearchPage> search(const SearchRequest &request,
                            std::stop_token stop) override;
  Result<ResolvedStream> resolve(const Track &track,
                                 std::stop_token stop) override;

  static std::string md5Hex(std::string_view input);

private:
  std::string authenticatedUrl(std::string_view endpoint,
                               std::string_view extra_query) const;

  SubsonicSettings settings_;
  std::shared_ptr<const HttpClient> http_;
  SaltFactory salt_factory_;
};

} // namespace termusic::streaming
