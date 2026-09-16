#include "streaming/provider.hpp"

namespace termusic::streaming {

Result<SearchPage> Provider::search(const SearchRequest &, std::stop_token) {
  return Result<SearchPage>::failure(
      {ErrorCode::Unsupported, "This provider does not support search", {}});
}

Result<ResolvedStream> Provider::resolve(const Track &, std::stop_token) {
  return Result<ResolvedStream>::failure(
      {ErrorCode::Unsupported,
       "This provider cannot resolve playback URLs",
       {}});
}

} // namespace termusic::streaming
