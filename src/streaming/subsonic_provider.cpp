#include "streaming/subsonic_provider.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace termusic::streaming {
namespace {

std::uint32_t rotateLeft(std::uint32_t value, std::uint32_t count) {
  return (value << count) | (value >> (32U - count));
}

std::string urlEncode(std::string_view value) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (const unsigned char byte : value) {
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
        byte == '.' || byte == '~') {
      result.push_back(static_cast<char>(byte));
    } else {
      result.push_back('%');
      result.push_back(hex[byte >> 4U]);
      result.push_back(hex[byte & 0x0FU]);
    }
  }
  return result;
}

std::string xmlDecode(std::string value) {
  const std::pair<std::string_view, std::string_view> entities[] = {
      {"&amp;", "&"}, {"&quot;", "\""}, {"&apos;", "'"},
      {"&lt;", "<"},  {"&gt;", ">"}};
  for (const auto &[entity, replacement] : entities) {
    for (std::size_t at = value.find(entity); at != std::string::npos;
         at = value.find(entity, at + replacement.size()))
      value.replace(at, entity.size(), replacement);
  }
  return value;
}

std::string attribute(std::string_view tag, std::string_view name) {
  const std::string key = " " + std::string(name) + "=\"";
  const std::size_t begin = tag.find(key);
  if (begin == std::string_view::npos)
    return {};
  const std::size_t value_begin = begin + key.size();
  const std::size_t end = tag.find('"', value_begin);
  if (end == std::string_view::npos)
    return {};
  return xmlDecode(std::string(tag.substr(value_begin, end - value_begin)));
}

double number(std::string_view value) {
  double result = 0.0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  return parsed.ec == std::errc{} ? result : 0.0;
}

std::string randomSalt() {
  std::random_device source;
  std::uniform_int_distribution<unsigned> distribution(0, 255);
  std::ostringstream value;
  value << std::hex << std::setfill('0');
  for (int index = 0; index < 12; ++index)
    value << std::setw(2) << distribution(source);
  return value.str();
}

Error responseError(const HttpResponse &response) {
  if (response.error == "cancelled")
    return {ErrorCode::Cancelled, "Subsonic search was cancelled", {}};
  if (!response.error.empty())
    return {ErrorCode::Network, response.error, {}};
  if (response.status == 401 || response.status == 403)
    return {ErrorCode::Authentication,
            "Subsonic authentication was rejected", {}};
  return {ErrorCode::Network,
          "Subsonic returned HTTP " + std::to_string(response.status), {}};
}

std::optional<Error> apiError(std::string_view xml) {
  const std::size_t root = xml.find("<subsonic-response");
  if (root == std::string_view::npos)
    return Error{ErrorCode::ProviderFailure,
                 "Subsonic returned malformed XML", {}};
  const std::size_t root_end = xml.find('>', root);
  if (root_end == std::string_view::npos)
    return Error{ErrorCode::ProviderFailure,
                 "Subsonic returned malformed XML", {}};
  if (attribute(xml.substr(root, root_end - root + 1), "status") != "failed")
    return std::nullopt;
  const std::size_t error_begin = xml.find("<error", root_end);
  const std::size_t error_end = xml.find('>', error_begin);
  const std::string_view tag =
      error_begin == std::string_view::npos || error_end == std::string_view::npos
          ? std::string_view{}
          : xml.substr(error_begin, error_end - error_begin + 1);
  const std::string code = attribute(tag, "code");
  const std::string message = attribute(tag, "message");
  const bool authentication = code == "40" || code == "41" || code == "44";
  return Error{authentication ? ErrorCode::Authentication
                              : ErrorCode::ProviderFailure,
               message.empty() ? "Subsonic request failed" : message, {}};
}

} // namespace

SubsonicProvider::SubsonicProvider(SubsonicSettings settings,
                                   std::shared_ptr<const HttpClient> http,
                                   SaltFactory salt_factory)
    : settings_(std::move(settings)), http_(std::move(http)),
      salt_factory_(std::move(salt_factory)) {
  while (!settings_.server_url.empty() && settings_.server_url.back() == '/')
    settings_.server_url.pop_back();
  if (!salt_factory_)
    salt_factory_ = randomSalt;
}

ProviderInfo SubsonicProvider::info() const {
  return {"subsonic", "Subsonic / Navidrome",
          Capability::Search | Capability::Resolve};
}

std::string SubsonicProvider::authenticatedUrl(
    std::string_view endpoint, std::string_view extra_query) const {
  const std::string salt = salt_factory_();
  const std::string token = md5Hex(settings_.password + salt);
  std::string url = settings_.server_url + "/rest/" + std::string(endpoint) +
                    ".view?u=" + urlEncode(settings_.username) +
                    "&t=" + token + "&s=" + urlEncode(salt) +
                    "&v=1.16.1&c=termusic&f=xml";
  if (!extra_query.empty())
    url += "&" + std::string(extra_query);
  return url;
}

Result<SearchPage> SubsonicProvider::search(const SearchRequest &request,
                                             std::stop_token stop) {
  if (!settings_.valid())
    return Result<SearchPage>::failure(
        {ErrorCode::Authentication, "Subsonic is not configured", {}});
  if (request.query.empty() || request.limit == 0)
    return Result<SearchPage>::failure(
        {ErrorCode::InvalidRequest, "Subsonic search needs a query", {}});
  const std::string query =
      "query=" + urlEncode(request.query) +
      "&artistCount=0&albumCount=0&songCount=" +
      std::to_string(request.limit);
  const HttpResponse response =
      http_->get(authenticatedUrl("search3", query), settings_.timeout_ms, stop);
  if (!response.error.empty() || response.status < 200 || response.status >= 300)
    return Result<SearchPage>::failure(responseError(response));
  if (const auto error = apiError(response.body))
    return Result<SearchPage>::failure(*error);

  SearchPage page;
  std::size_t position = 0;
  while ((position = response.body.find("<song", position)) !=
         std::string::npos) {
    const std::size_t end = response.body.find('>', position);
    if (end == std::string::npos)
      break;
    const std::string_view tag(response.body.data() + position,
                               end - position + 1);
    Track track;
    track.provider_id = "subsonic";
    track.track_id = attribute(tag, "id");
    track.title = attribute(tag, "title");
    track.artist = attribute(tag, "artist");
    track.album = attribute(tag, "album");
    track.artwork_uri = attribute(tag, "coverArt");
    track.duration_seconds = number(attribute(tag, "duration"));
    if (track.valid())
      page.tracks.push_back(std::move(track));
    position = end + 1;
  }
  return Result<SearchPage>::success(std::move(page));
}

Result<ResolvedStream>
SubsonicProvider::resolve(const Track &track, std::stop_token stop) {
  if (stop.stop_requested())
    return Result<ResolvedStream>::failure(
        {ErrorCode::Cancelled, "Subsonic resolution was cancelled", {}});
  if (!settings_.valid() || track.provider_id != "subsonic" ||
      track.track_id.empty())
    return Result<ResolvedStream>::failure(
        {ErrorCode::InvalidRequest, "Invalid Subsonic track", {}});
  return Result<ResolvedStream>::success(
      {authenticatedUrl("stream", "id=" + urlEncode(track.track_id)), {}});
}

std::string SubsonicProvider::md5Hex(std::string_view input) {
  static constexpr std::array<std::uint32_t, 64> shifts = {
      7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
      5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
      4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  std::array<std::uint32_t, 64> constants{};
  for (std::size_t index = 0; index < constants.size(); ++index)
    constants[index] = static_cast<std::uint32_t>(
        std::floor(std::abs(std::sin(static_cast<double>(index + 1))) *
                   4294967296.0));

  std::vector<std::uint8_t> data(input.begin(), input.end());
  const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
  data.push_back(0x80U);
  while (data.size() % 64U != 56U)
    data.push_back(0U);
  for (unsigned byte = 0; byte < 8; ++byte)
    data.push_back(static_cast<std::uint8_t>(bit_length >> (byte * 8U)));

  std::uint32_t a0 = 0x67452301U;
  std::uint32_t b0 = 0xefcdab89U;
  std::uint32_t c0 = 0x98badcfeU;
  std::uint32_t d0 = 0x10325476U;
  for (std::size_t offset = 0; offset < data.size(); offset += 64U) {
    std::array<std::uint32_t, 16> words{};
    for (std::size_t word = 0; word < words.size(); ++word) {
      for (unsigned byte = 0; byte < 4; ++byte)
        words[word] |= static_cast<std::uint32_t>(
                           data[offset + word * 4U + byte])
                       << (byte * 8U);
    }
    std::uint32_t a = a0;
    std::uint32_t b = b0;
    std::uint32_t c = c0;
    std::uint32_t d = d0;
    for (std::uint32_t index = 0; index < 64U; ++index) {
      std::uint32_t value = 0;
      std::uint32_t word = 0;
      if (index < 16U) {
        value = (b & c) | (~b & d);
        word = index;
      } else if (index < 32U) {
        value = (d & b) | (~d & c);
        word = (5U * index + 1U) % 16U;
      } else if (index < 48U) {
        value = b ^ c ^ d;
        word = (3U * index + 5U) % 16U;
      } else {
        value = c ^ (b | ~d);
        word = (7U * index) % 16U;
      }
      value += a + constants[index] + words[word];
      a = d;
      d = c;
      c = b;
      b += rotateLeft(value, shifts[index]);
    }
    a0 += a;
    b0 += b;
    c0 += c;
    d0 += d;
  }

  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const std::uint32_t word : {a0, b0, c0, d0}) {
    for (unsigned byte = 0; byte < 4; ++byte)
      output << std::setw(2) << ((word >> (byte * 8U)) & 0xffU);
  }
  return output.str();
}

} // namespace termusic::streaming
