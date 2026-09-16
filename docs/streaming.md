# Streaming architecture

Streaming is a first-class termusic subsystem under `src/streaming`; it is not
part of the native extension ABI. The framework deliberately separates three
different values that local MPD entries can normally treat as one:

- `provider_id`: the service implementation (`subsonic`, `direct-url`, ...);
- `track_id`: the provider's stable media identity;
- resolved URI: a URL MPD can play now, which may be signed and short-lived.

`provider_id + track_id` is the durable identity. Never persist a resolved URI
as the identity of a remote track.

## Adding a provider

Implement `streaming::Provider`, advertise only the supported capabilities,
and register one shared instance with `streaming::Service`. A searchable
provider implements `search`; a playable provider implements `resolve`.
Expected remote failures are returned as `streaming::Result`, not thrown.

Provider methods accept `std::stop_token`. Network implementations must observe
it and set finite connection and request timeouts. They will be called from a
worker, never synchronously from the FTXUI event/render path.

Core includes `DirectUrlProvider` as the dependency-free baseline for HTTP(S)
radio and direct audio streams. It validates and resolves URLs but does not
pretend that arbitrary URLs form a searchable catalog.

## Subsonic-compatible libraries

`SubsonicProvider` supports Navidrome, Gonic, Airsonic and other compatible
servers. Configure it under `Core > General > Online music library`, using the
server root URL without `/rest`. It calls `search3` for song results and builds
an authenticated `stream` URL for the selected stable song ID.

Authentication follows Subsonic API 1.16.1: every request generates a random
salt and sends the lower-case MD5 token of `password + salt`; the clear-text
password is not sent in the URL. The password is stored in termusic's config,
which is written with owner-only permissions when it contains a secret. HTTPS
is still required to protect metadata and tokens in transit.

HTTP calls use libcurl, follow a bounded number of redirects, have finite
connect/total timeouts, and observe the Agent worker's stop token.
Authenticated stream URLs live only in the active MPD queue. History persists
the stable Provider/song identity and resolves a fresh URL when replayed, so
Subsonic tokens are not written to `history.toml`.

## Using direct streams

Open `Vault > Streams`, press `a`, enter an HTTP or HTTPS audio/radio URL, and
press Enter. The entry is written atomically to
`$XDG_DATA_HOME/termusic/streams.toml` (normally
`~/.local/share/termusic/streams.toml`). Press Enter on a stream to replace the
MPD queue with the Streams snapshot and start playback. `dd` removes the
selected entry; Visual mode can remove a range.

## Playback bridge

Call `Service::resolve` immediately before queueing, then convert the pair with
`Service::playableSong`. The resulting `Song::uri` is consumable by the current
MPD backend, while the constructed queue entry carries `source_id` and
`source_track_id`. Live items set `Song::is_live_stream`, allowing future UI
controls to disable duration-based seeking.

MPD does not echo termusic's provider fields back in its queue/player metadata.
The controller correlates MPD queue positions with the playback snapshot and
restores that provenance before writing History. The current local-library flow
is unchanged.

## Agent integration

The top-level Agent merges the complete local MPD search, saved Streams, and
every registered searchable Provider. Remote catalog work runs on a cancellable
worker; `Service::searchAll` reports partial success, so one offline or
rate-limited service cannot erase results from healthy services. The selection
and ranking implementation is isolated in `src/agent`. It may filter and rank
returned tracks, but it never invents a track ID or playback URL. Core resolves
and validates provider results before they enter MPD's queue.
