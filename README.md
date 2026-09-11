# Termusic

**termusic is a lightweight terminal music client powered by MPD.**

It is a *client*, not an audio engine: the Music Player Daemon owns playback,
the decoder, the audio output, the media database and the queue, and termusic
provides the terminal interface, the Vault/Core structure, its own playback
history and configuration, and controls playback through MPD. Everything on
screen is a snapshot of MPD's state plus the actions termusic sends it.

MPD may run on the same machine or on a remote server. `127.0.0.1:6600` is the
built-in default, termusic connects automatically at startup, and another
endpoint is one visit to **Core → Connection** away.

termusic never installs, starts, stops, restarts or configures the MPD daemon.
That lifecycle belongs to the user or to the system; termusic only connects.

## Quick start

1. Have an MPD server reachable. For a local one, install MPD, give it a
   `music_directory`, build its database once and start it — that setup is MPD's
   own and is documented in the MPD manual. For a remote one, nothing local is
   needed at all.
2. Install termusic (see [Installing](#installing) or run the built binary).
3. Run it:

   ```bash
   termusic
   ```

   It tries `127.0.0.1:6600` immediately. Nothing to pair, nothing to confirm.

For another endpoint, either use Core → Connection inside the application, or
override it for one run / one file:

```bash
termusic --host musicbox.lan --port 6600
MPD_HOST=musicbox.lan MPD_PORT=6600 termusic
```

If MPD is not reachable, termusic still opens: the UI works, Core works, and
Core → Connection shows `disconnected` with the reason (`Connection refused`,
`Timed out`, `Authentication failed`) so the settings can be repaired on the
spot. Nothing about a missing server is fatal.

**MPD itself still needs a valid `music_directory`, a built database, an
`audio_output` and a running service/process.** termusic can only connect to a
server that is already configured and running; it does not set one up for you.

## Build

The project needs FTXUI, libmpdclient, FFTW3 (single precision),
CMake 3.20+ and a C++20 compiler. `libmpdclient` and `fftw3f` are discovered
through pkg-config; FTXUI is provided by the vcpkg manifest.

The build scripts publish two artifacts, and these are the only paths a user
needs to know:

| command | artifact |
|---|---|
| `./build/make.sh` | `build/termusic` (release) |
| `./build/make.sh --debug` | `build/termusic-debug` (assertions on) |

```bash
export VCPKG_ROOT=/path/to/vcpkg
./build/make.sh                      # -> build/termusic
./build/make.sh --debug              # -> build/termusic-debug
ctest --test-dir build/dev --output-on-failure   # the test suite
```

Each script deletes its own previous artifact before compiling, so a failed
build cannot leave a stale executable at the published path, and it leaves the
other variant's artifact untouched. Everything else under `build/` (for example
`build/release/termusic`) is an internal build-directory output; it is never the
thing to run.

Run it with the normal `MPD_HOST` / `MPD_PORT` environment, or override the
connection for one invocation:

```bash
./build/termusic
./build/termusic --host 127.0.0.1 --port 6600
```

### Playback context

Playback belongs to the collection it was started from. Pressing Enter on a row
makes that collection the **playback context** and snapshots its tracks; the
playing row is painted only in that collection, and Previous / Next / automatic
progression walk the snapshot. Browsing another collection -- even one holding
the exact same URI -- shows no marker, and Player Bar / Immersive always show
the real current track.

The marker therefore needs BOTH halves: the displayed collection must be the
context, AND the row must be the occurrence (a duplicate-aware count for a list,
a record id for History). A URI match alone is never enough: the same URI lives
in Library, in History and in several playlists at once, and History can hold it
twice.

Editing a collection or deleting a History record does not mutate a run that is
already under way -- only the next Enter takes a new snapshot. Playback that termusic did not start (an MPD client already
playing, or another client changing tracks) has no context, so nothing is
marked rather than guessing one from a URI.

### History

History is the persistent log of playback occurrences: duplicates allowed,
newest first in the view, capped by `[history] max_entries` (100 by default).
It is application-owned data in termusic's XDG data directory
(`~/.local/share/termusic/history.toml`), never an MPD playlist, and `[history]
enabled = false` stops termusic from reading or writing it at all. `dd` removes the selected record and `v` +
`d` removes a selected range; deletion edits application-owned storage only, so
no file, Library row, saved-playlist entry or queue item is touched. A record is
identified by its own id, so removing one of two same-URI occurrences removes
exactly that one, and the deletion survives a restart.

### Build artifacts

Every change is delivered as a runnable binary, not just as source:

```bash
./build/make.sh            # -> build/termusic  (optimized; this is the one to run)
./build/make.sh --debug    # -> build/termusic-debug (assertions on) + test binaries
ctest --test-dir build/dev --output-on-failure
```

Run `./build/termusic`. Each run **deletes the previous executable first** and
compiles a new one, so `build/` holds exactly one binary and it always matches
the current source; a failed build leaves nothing stale behind to run by
mistake. The build prints the time, size and md5 of what it produced.

Both scripts point CMake at `build/vcpkg-root`, a writable overlay of the vcpkg
root (its `scripts`/`ports`/`triplets` are symlinked and its installed tree is
copied) because vcpkg takes a lock on the real root during a reconfigure, and
that path is not always writable. They also touch the sources before building:
on this filesystem a write and an edit stamp different clocks, which makes
ninja's mtime comparison unreliable. Use them instead of a bare
`cmake --build`.

### Interactive checks

`build/livecheck/` holds a throwaway MPD instance (its own music directory,
port and configuration). Two kinds of check run against it.

**Scripted, headless** (`--ui-script FILE`): drives the real application — the
real key dispatch, the real timer tick, the real render tree — and asserts on
its own state and on the frame it produced. Commands: `size WxH`, `resize WxH`,
`load`, `key TOKEN`, `keys …`, `type "chord"`, `state`, `dump`,
`expect TEXT`, `expect-absent TEXT`, `expect-row N TEXT`, `expect-state K=V …`,
`sleep S`, `quit`.

```bash
BASE=build/livecheck
XDG_CONFIG_HOME=$BASE/config XDG_DATA_HOME=$BASE/data \
  XDG_CACHE_HOME=$BASE/cache XDG_STATE_HOME=$BASE/state \
  ./build/termusic --host 127.0.0.1 --port 6621 \
  --ui-script $BASE/scripts/context.ui
```

`expect-state` is what makes the playback rules checkable: the marker and the
playback context are state, not pixels, so a script asserts `marker=1
occ=1 seq=3` instead of a screenshot of it.

**Interactive, in tmux**: `verify.sh`, `qprobe.sh` and `im_visual.sh` launch the
real TUI and capture panes with colour. `viz_matrix.sh` runs every visualizer
style at all five supported sizes, with and without signal, and
`viz_matrix_check.py` measures the frames: the information bar and the Player
Bar are untouched, everything a style draws stays inside the visualizer region,
the grid is centred, and silence clears. `viz_switch.sh` cycles the styles
repeatedly while a track is playing and checks that playback never stops.

The UI responds to terminal resizing; see “Layout” below for how the panels
scale and for the minimum supported size.

## MPD visualizer output

The spectrum never generates random or sine-wave placeholder data. It reads
real signed 16-bit little-endian PCM from a second MPD FIFO output. Add an
output like this to `mpd.conf`, in addition to the normal audible output:

```conf
audio_output {
    type   "fifo"
    name   "termusic-visualizer"
    path   "/tmp/mpd.fifo"
    format "44100:16:2"
}
```

Restart MPD after changing its configuration. The FIFO path, sample rate and
channel count must match `~/.config/termusic/config.toml`. If the FIFO is
missing or has no writer, the visualizer remains at zero instead of showing
fake activity.

## Configuration

Configuration lives in one XDG file:

```
$XDG_CONFIG_HOME/termusic/config.toml      (~/.config/termusic/config.toml)
```

Nothing is written into the working directory, the source tree, `$HOME` itself
or any MPD-owned directory. The file is **not** created just because termusic
started: it appears the first time a setting is actually changed, so a fresh
install leaves no file behind. `termusic --print-default-config` prints the
complete reference document (every key, commented) to stdout.

See [`packaging/config.example.toml`](packaging/config.example.toml) for the
generated reference. The full schema (`schema_version = 1`):

```toml
schema_version = 1

[general]
start_page = "library"     # library (the Vault) | core
stop_on_exit = true        # false: quitting only disconnects, MPD keeps playing
seek_step = 5
volume_step = 5

[library]
path = ""                  # informational; MPD owns its own music_directory

[mpd]
host = "127.0.0.1"         # or a hostname, an address, or a socket path
port = 6600
password = ""              # prefer this file (0600) over the command line
timeout_ms = 2000          # never 0: 0 means "wait forever" to libmpdclient
auto_reconnect = true

[appearance]
theme = "default"          # resolves to catppuccin-mocha
theme_directory = ""       # default: <config>/themes
icons = "nerd"             # nerd | unicode

[visualizer]
enabled = true
style = "classic-bars"     # classic-bars | waterfall | particles
palette = "theme"          # theme | ice | fire | rainbow
refresh_hz = 60
sensitivity = 1
bar_density = 64
fifo_path = "/tmp/mpd.fifo"
sample_rate = 44100
channels = 2

[history]
enabled = true
max_entries = 100

[plugins]
enabled = true
directory = ""             # default: <data>/termusic/plugins

[keybindings.global]       # only what you write here overrides a default
# toggle_immersive = "i"
```

### Precedence

For the MPD endpoint, the layers are resolved in this order:

**command line > environment > config.toml > built-in defaults**

So with `port = 6601` in the file, `MPD_PORT=6602` in the environment and
`--port 6621` on the command line, termusic connects to **6621**.
`termusic --check-config` prints the effective value *and* which layer won.
The environment variables are the conventional MPD ones:

| variable | meaning |
|---|---|
| `MPD_HOST` | host, or `password@host`, or an absolute socket path |
| `MPD_PORT` | port number |
| `TERMUSIC_CONFIG` | config file to use (`--config` wins over it) |

Command-line overrides are **never written back**: a one-off `--host` cannot
end up in `config.toml` because a theme was changed later.

### Validation

Values are checked when the file is read. Out-of-range or unparsable values are
reported (and are a non-zero exit for `--check-config`) and replaced by the
safe built-in default — a broken file never crashes termusic:

| key | accepted | fallback |
|---|---|---|
| `mpd.port` | 0–65535 (0 = "not set here") | 6600 |
| `mpd.timeout_ms` | 100–60000 | 2000 |
| `general.seek_step` / `volume_step` | 1–60 / 1–25 | 5 / 5 |
| `visualizer.refresh_hz` | 5–60 | 60 |
| `visualizer.sensitivity` | 0.1–5.0 | 1 |
| `visualizer.bar_density` | 32–96 | 64 |
| `visualizer.sample_rate` / `channels` | 8000–192000 / 1–8 | 44100 / 2 |
| `history.max_entries` | 1–10000 | 100 |

### Saving

Configuration is written **atomically**: the new document goes to a temporary
file in the same directory, is flushed to disk, and replaces `config.toml` in a
single rename. A crash, a full disk or a kill during a save leaves either the
old file or the new one, never a truncated one. A file that carries a password
is created with owner-only permissions (`0600`).

Keys termusic does not model are **preserved**: change the theme in Core and a
setting from a newer version, a plugin or your own notes' keys still survive the
rewrite. Two things are deliberately *not* preserved, because the reader is
line-based and the writer regenerates the document: **comments** and the exact
order of sections. `termusic --check-config` reports how many unknown keys were
kept.

### Where the rest lives

| what | path |
|---|---|
| configuration | `$XDG_CONFIG_HOME/termusic/config.toml` (fallback `~/.config/termusic/`) |
| themes | `$XDG_CONFIG_HOME/termusic/themes/` |
| playback history | `$XDG_DATA_HOME/termusic/history.toml` (fallback `~/.local/share/termusic/`) |
| native plugins | `$XDG_DATA_HOME/termusic/plugins/` |
| diagnostics log | `$XDG_CACHE_HOME/termusic/termusic.log` (written only when something fails) |

`XDG_STATE_HOME/termusic/history.toml`, where history used to live, is still
read once if the new file does not exist yet; nothing is written there any more.

## Command line

```
termusic --help                 show every option
termusic --version              print "termusic <version>" and exit
termusic --config PATH          use this configuration file
termusic --check-config         validate the configuration and exit
termusic --print-default-config print the reference configuration and exit
termusic --host HOST            server name, address or socket path
termusic --port PORT            server port
termusic --password VALUE       password (prefer config.toml or MPD_HOST:
                                command lines are visible in `ps`)
```

`--check-config` never starts the UI, never connects to MPD and never writes
anything; it exits non-zero when the file asks for something invalid. It prints
the resolved endpoint with its origin, the timeout, the directories in use and
any warnings — and never the password:

```
$ termusic --check-config
Config: /home/user/.config/termusic/config.toml
Schema: 1
Status: valid
MPD: 192.168.1.20:6600  [environment (MPD_HOST) / default]
Password: set (hidden)  [config.toml]
Timeout: 2000 ms
Auto reconnect: true
History: enabled, up to 100 entries
Data directory: /home/user/.local/share/termusic
Cache directory: /home/user/.cache/termusic
Plugin directory: /home/user/.local/share/termusic/plugins
```

`--print-default-config` writes a complete document to stdout, so

```bash
termusic --print-default-config > config.toml
termusic --config config.toml --check-config    # Status: valid
```

is a supported way to start from a commented reference.

## Installing

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build/release
sudo cmake --install build/release        # ${prefix}/bin/termusic + docs
```

The install step ships the binary, the plugin header and the documents; tests,
fixtures and captures are never installed. Nothing at runtime depends on the
source tree or the working directory.

A release archive can be produced with:

```bash
./build/release.sh
# -> build/dist/termusic-<version>-linux-<arch>.tar.gz
#    termusic, README.md, LICENSE, THIRD_PARTY_LICENSES.md, config.example.toml
```

No distribution packages are published for 0.1.0: `packaging/` holds an RPM spec
and Debian metadata, and `packaging/README.packaging` records exactly which of
them have actually been built with the local tooling.

Runtime dependencies (verify with `ldd`): `libmpdclient.so.2` and
`libfftw3f.so.3`, plus the C++ runtime. **The MPD daemon is not a hard
dependency** — a remote MPD needs no local one — so packages should list it as
optional/recommended (`Recommends:`/`Suggests:`). `packaging/` holds an RPM
spec, Debian metadata and `README.packaging` with the details; termusic ships no
MPD service unit and no scriptlet that would touch one.

Themes and native plugins are discovered from `themes/` beside the config file
and from the plugins directory in the XDG data directory. Termusic includes
`default` and `nord`; user theme files and plugin-provided themes appear in
Core → Appearance. Plugins can contribute themes, typed setting descriptors and
UI blocks without depending on FTXUI. See
[Themes and plugins](docs/extensions.md) for the file format, versioned C ABI
and a minimal plugin example. Native plugins are trusted in-process code; use
`--no-plugins` when needed.

## Layout

The interface is organised by thin rounded panels, top to bottom:

1. **Navigation bar** — `♫ termusic`, Library / Settings, the slogan and a
   live clock when space permits.
2. **Main area** — Library is a collection tree plus a track buffer. Settings
   owns the same region. The tree holds one `Vault` root with `Library` (the
   read-only media database, opened at startup), `History`, and `PlayLists`
   with the saved playlists below it, plus a `Core` root for configuration.
   MPD's runtime queue is not in the tree: it is the mechanism termusic plays a
   snapshot through, not a collection to browse. Every playlist row is
   user-owned -- a playlist called `default`, `like` or `favorites` is listed
   and playable like any other, and connecting never rewrites it.
3. **Player bar** — shuffle, previous, play/pause, next, repeat, elapsed time,
   the progress capsule, total time, volume and the volume percentage.
4. **Interaction box** — empty unless it is carrying something: the search
   field, a playlist-name prompt, or a destructive confirmation. It is drawn as
   a framed input with a white border, and it carries no key legend. There is no
   permanent status or hint line; short-lived feedback appears as a toast in the
   bottom-right corner and expires on its own.

### Saved playlists are user data

Connecting to MPD is **observational**: termusic reads status, the current song,
the queue, the database and the playlist names, and writes nothing. In
particular there is no "mirror" playlist: starting playback from the Library
puts its snapshot into MPD's *runtime queue* through the playback action itself,
and no saved playlist is created, cleared, rebuilt or deleted on connect, on
reconnect, or when the database changes.

A saved playlist called `default` is therefore treated like any other
user-owned playlist: it is listed, browsable, playable and editable, and
termusic never overwrites it. `default` is not a reserved name anywhere in the
code -- there is no built-in node it could collide with, and the runtime queue
gets no row of its own.
`build/livecheck/user_playlist_safety.sh` is the regression test: it creates a
`default` playlist with contents that exist nowhere in the library, runs
termusic three times against it, and fails if a single byte changes.

### Now-playing block

The lower-left corner of the sidebar is a display-only anchor, four lines built
from the bottom up and fainter as they go up:

```
┃ Aurora Fields     faintest  (weak text, dimmed)
┃ Nightfall         body      (body text, bold)
┃ # Library         context   (secondary accent)
┃ NOW PLAYING       the anchor (primary accent, bold)
```

The context line names the collection that OWNS the playback run — the same
authority the playing marker uses, never the row the cursor happens to be on, so
browsing History while a Library run plays keeps showing `# Library`. It reads
`# <playlist name>` (the saved playlist the run was started from, whatever it is
called), `# Library` or `# History`; a run termusic did not start shows the song
without inventing an owner.

The block is one accent bar (a single cell, an existing theme colour, static —
it never follows playback, the beat or the cursor), one cell of air, and the
text. It is not focusable, not clickable, not searchable and carries no hint
text. There is no gap above it and no padding below it: `NOW PLAYING` sits on
the sidebar's last row, and the flexible space above the tree is the only
separation.

With nothing playing the block is **hidden** (no stale metadata, no empty bar);
the rows stay reserved, so starting or stopping playback never moves the tree.
Pausing keeps all four lines. Every line is truncated to the column with an
ellipsis, never wrapped, and when the sidebar is short the block gives up its
faintest line first — the artist, then the context — then disappears, so the
tree is never squeezed for it.

The layout responds continuously to terminal resizing. At narrow widths the
Library shows the focused pane rather than squeezing both panes into unusable
columns, and optional header/player details are progressively removed.


### Immersive now-playing

`i` overlays the current section with the now-playing screen, without changing
its selection or scroll position. It is a single centered column inside the same
frame as the rest of the application, above the Player Bar:

```
+-------------------------------- frame --------------------------------+
|                        Nightfall          (centered, bold, accent)    |  information bar
|                       Aurora Fields       (centered, dimmed)          |
|                                                                       |  logical gap (space)
|                                                                       |
|                     V I S U A L I Z E R                               |  the primary content
|                                                                       |
|                                                                       |  logical gap (space)
|   shuffle  prev  play  next      timeline           volume            |  Player Bar
+-----------------------------------------------------------------------+
```

**The screen holds four things and nothing else**: the title, the artist, the
visualizer and the Player Bar. No artwork of any kind, no placeholder for it, no
lyrics, and no Album, Year, Genre, Format or Length — those stay in the model and
in the workspace table, where a question about a file belongs.

**The visualizer is the only primary content**, and it is one of three styles:

| style | what it draws |
|---|---|
| `classic-bars` (default) | one rectangular bar per frequency slot on a common baseline of small cells, with a decaying peak marker |
| `waterfall` | a bounded history of spectrum cells, newest at the bottom, older rows fading as they scroll upward |
| `particles` | rectangular particles emitted by the columns that have energy, rising and dying; a beat makes the burst stronger |

All three consume the same model — the analyzer's bands, its peak-hold values,
and the beat envelope — and none of them owns an analyzer of its own. The
**palette** is a separate choice (`theme`, `ice`, `fire`, `rainbow`), so any
style draws in any ramp; `theme` follows the active UI theme. Both selectors
live under Core > Appearance and switch live, with no restart and no pause in
playback: the renderer is rebuilt from the registry and reset, so no state can
leak from one style to the next.

Styles are registered in `src/ui/visualizer/registry.cpp` — a fourth one is a new
file plus one registry entry. An unknown or removed identifier (a stored
`city`, a typo) normalizes to `classic-bars`, which is also the fallback if a
renderer cannot be built; the city renderer is gone, not hidden.

There is no braille, no dust, no reflection and no drawn ground line in any
style. In silence the styles stop drawing what came from audio: bars keep only
their baseline, the waterfall drains, and the particles fall out of existence.

The beat is derived rather than read: `visualizer/beat.cpp` keeps a rolling
baseline of low-band energy, fires an onset when the fast envelope rises above
the slow one, and applies a refractory window plus a Schmitt re-arm so one hit
gives exactly one pulse. Bars use it as a baseline accent, particles as a burst
boost.

**Both separations are space, not chrome**: the information bar is separated
from the visualizer, and the visualizer from the Player Bar, by blank rows sized
in the layout (`info_gap_rows`, `player_gap_rows`). No rule, border or separator
glyph is ever drawn, and both gaps shrink to one row before the spectrum loses a
row.

The visualizer's box is a pure function of the terminal size — the animation
only changes the glyphs inside it — so at a fixed size the title box, the artist
box, the visualizer container and the Player Bar rows are byte-identical from
frame to frame.

### Player Bar playback modes

The bar carries **one** playback-mode control: Shuffle. Sequential playback is
simply `random = off` — there is no second icon, no "SEQ" label and no arrows.
The icon is always visible; its highlight *is* the state, read from MPD's own
`random` flag on every frame:

| MPD `random` | Shuffle control |
|---|---|
| off (sequential) | visible, muted |
| on (shuffle) | highlighted with the active accent |

Activating it toggles MPD's `random`; nothing is cached in the UI, so a
collection change or a restart can never leave a stale highlight. Repeat is
still an action (`r`, and the keymap is unchanged), it is simply no longer one
of the Player Bar's controls, and no blank space is left where it used to be:
the cluster is four buttons wide and the progress track absorbs the cells.
### Icons

The Vault tree and the track table carry one small icon per row, from a single
table of glyphs in `src/ui/widgets.*`:

| row | icon |
|---|---|
| Vault / Core root | folder |
| a group, collapsed / expanded | closed / open folder |
| Library | music note |
| History | history |
| a saved playlist | list |
| every song row | music note |

The tree carries no disclosure triangle: the folder icon already says whether a
group is open (closed / open folder), so a second arrow was noise. The leading
slot keeps its exact two cells on every row -- the `●` that marks the open
collection, blank otherwise -- and the icon follows it, so state and kind are
never confused and no row grows a gutter; `core`'s settings entries deliberately
carry no icon. A track row's music icon means "this is a song" -- it is not the
playing marker, which stays in its own reserved column. Icons are one cell wide
and budgeted as a real column, so adding them cannot move the artist or album
columns, and a terminal
without a Nerd Font gets the width-safe Unicode fallback (`▣ ▢ ♪ ↺ ≡`) instead
of an emoji.

## Controls

The key set is deliberately small; `Core > Help` shows the same list inside the
application.

- `1` / `2`: jump to the Vault root / the Core root
- `j` / `k`: move in the tree, and in the settings pane once it has the keyboard
- `h` / `l`: leave the pane / enter it
- `g g` / `G`: first / last row
- `PageDown` / `PageUp`: half page down / up (in the track list `Ctrl+d` /
  `Ctrl+u` do the same)
- `Enter`: expand a group, open a collection, play the selected track, or act on
  the setting under the cursor (flip a switch, cycle a choice, edit a value, run
  an action)
- `v`, `y y`, `d d`: select, yank or remove tracks; `p` pastes into a writable
  collection
- `K` / `J` in a saved playlist: move the selected track up / down
- `a` / `r` / `d d` in the tree: create, rename or delete a saved playlist
- `/`: open the search box at the bottom and search the focused pane (the tree,
  or whatever collection the track list is showing). The cursor follows what you
  type; `Enter` keeps the match, `Esc` puts the cursor back, and `n` / `N` walk
  the accepted match in the track list
- `Space`, `r`, `s`: play/pause, repeat and shuffle
- `i`: toggle immersive now-playing; there `h/l`, `j/k` and `,/.` control
  previous/next, volume and seeking
- `Esc`: close what is open (the box, a field, the pane)
- `q`: stop MPD playback, disconnect and quit. It is reserved: it always quits,
  and no configuration can take it away.

`Core > Keybindings` lists every editable action per context; the entries the
application fixes (the reserved `q`, the recovery control) are not offered
there. The collection tree, track rows, transport buttons, progress bar and
volume bar support the mouse: a single click selects/opens, a double click on
the same track plays it, and the two sliders can be dragged to seek or set
volume.

## License

termusic is free software, released under the **GNU General Public License,
version 3 or later** (`GPL-3.0-or-later`). The full text is in
[`LICENSE`](LICENSE); the packaged copies (`packaging/termusic.spec`,
`packaging/debian/control`) declare the same identifier.

Third-party components and their obligations are recorded in
[`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md): FTXUI (MIT) is linked
**statically**, so its notice travels with the binary, while libmpdclient (BSD)
and FFTW (GPL-2.0-or-later) are dynamic dependencies resolved from the operating
system's own packages.

## Source layout

- `src/backend`: the only module that includes libmpdclient
- `src/controller`: actions and MPD-to-AppState synchronization
- `src/ui`: FTXUI components and pure state rendering
- `src/visualizer`: FIFO reader, FFTW analysis, logarithmic bands and smoothing,
  plus the beat/onset detector. One analyzer for every style.
- `src/ui/visualizer`: the renderer layer — the style registry, the palette
  registry, the shared cell grid, and one file per style (`classic_bars`,
  `waterfall`, `particles`). A style consumes a `VisualizerFrame` (bands, peaks,
  beat, grid geometry, palette) and never touches the analyzer or the UI layout.
- `src/ui/core`: the `core` settings centre. `settings.hpp/.cpp` is the reusable
  framework: `SettingItem` descriptors (heading, text, toggle, input, number,
  select, action) plus the list engine that navigates, edits and draws them, and
  `SettingsListSection`, the base almost every module derives from. One file per
  module (`help`, `connection`, `general`, `appearance`, `keybindings`,
  `plugins`), and `panel.cpp` holds the section registry: the tree row comes
  from `kCoreSections` (state.hpp) and its factory from one line in
  `kSectionFactories`, with the ids checked at startup. A module describes which
  settings it has and how to read and write them; it never draws a control.
- `src/config`: the ONE configuration authority. `config.hpp/.cpp` holds the
  typed `Config` model, the reader/writer (schema version, validation, atomic
  save, unknown-key preservation) and `resolveConnection()`, which applies the
  documented precedence `command line > environment > config.toml > defaults`.
  `paths.hpp/.cpp` is the only place that knows the XDG layout, so a path is
  never re-derived somewhere else.
- `src/app`: application state, playback context, keymap, history and the
  diagnostics log (an XDG-cache file written only when something fails, with
  password redaction). `version.hpp` carries the ONE canonical version, injected
  by CMake from `project(termusic VERSION ...)`.
- `src/extensions`: versioned native-plugin ABI, loader and extension registry
- `tests`: deterministic state, UTF-8, config (including the release contract:
  defaults round-trip, atomic save, value validation, precedence and XDG
  resolution) and spectrum checks

The build is split into `termusic_core` (state, configuration, registries and
pure UI helpers) and `termusic_runtime` (MPD, plugin loading and the FTXUI
application). Tests link the same libraries as the executable instead of
recompiling private copies of production sources.
