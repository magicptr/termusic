#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace termusic::streaming {

/// What a provider can do. Capabilities are advertised explicitly so the UI
/// and the future selection agent never have to discover support by failing a
/// request first.
enum class Capability : unsigned {
  None = 0,
  Search = 1U << 0U,
  Resolve = 1U << 1U,
};

constexpr Capability operator|(Capability lhs, Capability rhs) {
  return static_cast<Capability>(static_cast<unsigned>(lhs) |
                                 static_cast<unsigned>(rhs));
}

constexpr bool hasCapability(Capability value, Capability flag) {
  return (static_cast<unsigned>(value) & static_cast<unsigned>(flag)) != 0U;
}

enum class ErrorCode {
  InvalidRequest,
  ProviderNotFound,
  Unsupported,
  Authentication,
  Network,
  RateLimited,
  NotFound,
  Cancelled,
  ProviderFailure,
};

struct Error {
  ErrorCode code = ErrorCode::ProviderFailure;
  std::string message;
  /// A provider may suggest when a rate-limited request can be retried.
  std::optional<std::chrono::seconds> retry_after;
};

/// Small C++20 result type. Streaming failures are expected runtime outcomes,
/// not exceptions: a remote source may be offline while local playback keeps
/// working normally.
template <typename T> class Result {
public:
  static Result success(T value) { return Result(std::move(value)); }
  static Result failure(Error error) { return Result(std::move(error)); }

  bool ok() const { return value_.has_value(); }
  explicit operator bool() const { return ok(); }
  const T &value() const { return *value_; }
  T &value() { return *value_; }
  T takeValue() { return std::move(*value_); }
  const Error &error() const { return error_; }

private:
  explicit Result(T value) : value_(std::move(value)) {}
  explicit Result(Error error) : error_(std::move(error)) {}

  std::optional<T> value_;
  Error error_;
};

/// Provider-neutral metadata. `provider_id + track_id` is the stable identity;
/// `playback_uri` is deliberately absent because signed URLs may expire.
struct Track {
  std::string provider_id;
  std::string track_id;
  std::string title;
  std::string artist;
  std::string album;
  std::string artwork_uri;
  double duration_seconds = 0.0;
  bool live = false;

  bool valid() const { return !provider_id.empty() && !track_id.empty(); }
  std::string stableId() const { return provider_id + ":" + track_id; }
};

struct SearchRequest {
  std::string query;
  std::size_t limit = 50;
  /// Opaque provider-owned continuation token. Core never parses it.
  std::string continuation;
};

struct SearchPage {
  std::vector<Track> tracks;
  std::string continuation;
};

/// A short-lived, MPD-consumable result. Providers refresh this immediately
/// before queueing instead of persisting it as track identity.
struct ResolvedStream {
  std::string uri;
  std::optional<std::chrono::system_clock::time_point> expires_at;
};

struct ProviderInfo {
  std::string id;
  std::string name;
  Capability capabilities = Capability::None;
};

struct ProviderFailure {
  std::string provider_id;
  Error error;
};

/// Cross-provider search is intentionally partial-success: one unavailable
/// service must not hide usable candidates returned by the others.
struct CatalogSearchResult {
  std::vector<Track> tracks;
  std::vector<ProviderFailure> failures;
};

} // namespace termusic::streaming
