#include "app/reconnect.hpp"

#include <algorithm>

namespace termusic {

std::chrono::milliseconds reconnectDelay(int attempt) {
  if (attempt < 1)
    attempt = 1;
  const std::size_t index =
      std::min(static_cast<std::size_t>(attempt - 1), kReconnectLadderMs.size() - 1);
  return std::chrono::milliseconds{kReconnectLadderMs[index]};
}

const char *reconnectLadderText() { return "3s 5s 10s 15s 30s..."; }

} // namespace termusic
