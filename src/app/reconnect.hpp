#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace termusic {

/// The delay before each automatic reconnect attempt, indexed by how many
/// attempts have already failed since the last successful connection (or since
/// the endpoint last changed).
///
/// The ladder is deliberately small and low-frequency: it starts fast enough
/// that a server which is simply starting up is picked up quickly, and it stops
/// growing at half a minute so a machine that is off does not get hammered.
///
///   attempt 1 -> 3 s    attempt 4 -> 15 s
///   attempt 2 -> 5 s    attempt 5+ -> 30 s
///   attempt 3 -> 10 s
///
/// There is NO attempt limit: `auto_reconnect = true` means "keep trying until
/// the connection succeeds, the user turns it off, the endpoint changes, or the
/// program exits". Giving up after a fixed number of tries would leave a client
/// that silently stays dead after a server restart.
inline constexpr std::array<int, 5> kReconnectLadderMs = {3000, 5000, 10000,
                                                          15000, 30000};

/// The delay before attempt number `attempt` (1-based). Anything past the end of
/// the ladder keeps the last value.
std::chrono::milliseconds reconnectDelay(int attempt);

/// The whole ladder, as a printable sequence: "3s 5s 10s 15s 30s".
const char *reconnectLadderText();

} // namespace termusic
