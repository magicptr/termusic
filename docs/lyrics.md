# Lyrics

Press `L` to open or close the lyrics overlay for the current song. `Esc` also
closes it. Synchronized LRC lines follow MPD's current playback position; plain
text lyrics are displayed without timing.

Set `[library].path` in `config.toml` to the same local music directory MPD
serves. For a song such as `Artist/Album/Track.flac`, termusic checks, in order:

1. `Artist/Album/Track.lrc`
2. `Artist/Album/Track.flac.lrc`
3. `Artist/Album/Track.txt`

Paths are constrained to the configured library root, including through
symlinks. Lyrics files larger than 2 MiB are rejected. A remote MPD server's
filesystem is not automatically available to the client; use a locally mounted
library path if sidecar lyrics should be read on another machine.

The parser supports multiple timestamps on one line, `[offset:...]`, `[ti:...]`
and `[ar:...]`. Malformed tags do not invalidate other usable lines.

Streaming providers do not currently supply lyrics. A future provider can add
remote lyrics behind the lyrics service without changing the overlay or LRC
timeline model.
