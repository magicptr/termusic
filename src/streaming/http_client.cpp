#include "streaming/http_client.hpp"

#include <algorithm>
#include <mutex>

#include <curl/curl.h>

namespace termusic::streaming {
namespace {

void initializeCurl() {
  static std::once_flag once;
  std::call_once(once, [] { (void)curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::size_t appendBody(char *data, std::size_t size, std::size_t count,
                       void *target) {
  const std::size_t bytes = size * count;
  static_cast<std::string *>(target)->append(data, bytes);
  return bytes;
}

int progress(void *state, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  return static_cast<const std::stop_token *>(state)->stop_requested() ? 1 : 0;
}

} // namespace

HttpResponse CurlHttpClient::get(std::string_view url, int timeout_ms,
                                 std::stop_token stop) const {
  initializeCurl();
  HttpResponse response;
  CURL *handle = curl_easy_init();
  if (handle == nullptr) {
    response.error = "Could not initialize HTTP client";
    return response;
  }
  const std::string owned_url(url);
  char error[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(handle, CURLOPT_URL, owned_url.c_str());
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                   static_cast<long>(std::max(100, timeout_ms / 2)));
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(std::max(100, timeout_ms)));
  curl_easy_setopt(handle, CURLOPT_USERAGENT, "termusic/0.2");
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendBody);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error);
  curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progress);
  curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &stop);
  const CURLcode code = curl_easy_perform(handle);
  if (code == CURLE_OK)
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status);
  else if (code == CURLE_ABORTED_BY_CALLBACK && stop.stop_requested())
    response.error = "cancelled";
  else
    response.error = error[0] != '\0' ? error : curl_easy_strerror(code);
  curl_easy_cleanup(handle);
  return response;
}

} // namespace termusic::streaming
