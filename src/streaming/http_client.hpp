#pragma once

#include <stop_token>
#include <string>
#include <string_view>

namespace termusic::streaming {

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;
};

class HttpClient {
public:
  virtual ~HttpClient() = default;
  virtual HttpResponse get(std::string_view url, int timeout_ms,
                           std::stop_token stop = {}) const = 0;
};

class CurlHttpClient final : public HttpClient {
public:
  HttpResponse get(std::string_view url, int timeout_ms,
                   std::stop_token stop = {}) const override;
};

} // namespace termusic::streaming
