# termusic

Termusic is a lightweight, keyboard-driven terminal music player focused on local music playback. It uses Vim-style keyboard shortcuts for efficient music selection and playback.It is designed for Linux users, terminal enthusiasts, Vim users, and anyone who wants a
fast, distraction-free way to manage and play a local music library without leaving the command line.

The in-tree streaming foundation provides provider-neutral catalog search and
short-lived playback URL resolution. Direct HTTP(S) streams are supported at
the framework level and can be managed under **Vault → Streams**. A built-in
Subsonic provider adds searchable Navidrome/Gonic/Airsonic libraries. See
[the streaming architecture](docs/streaming.md).

Local sidecar lyrics are available with `L`, including playback-synchronized
LRC files and ordinary text lyrics. Configure `[library].path` to MPD's local
music directory; see [Lyrics](docs/lyrics.md).

The top-level **Agent** workspace accepts music requests, searches the complete
local MPD library and saved Streams together, ranks the combined results, and
plays the selected result snapshot. See [Music Agent](docs/agent.md).


## Demo

![Library view](./docs/images/list.png)
![Now Playing view](./docs/images/play.png)

## Requirements

- A C++20-compatible compiler
- CMake 3.20 or later
- Git
- Meson
- Ninja
- libcurl development headers

Install the build dependencies on Ubuntu / Debian:

```bash
sudo apt update
sudo apt install build-essential cmake git meson ninja-build libcurl4-openssl-dev
```

Install the build dependencies on Fedora:

```bash
sudo dnf install gcc-c++ cmake git meson ninja-build libcurl-devel
```

FTXUI, libmpdclient, and kissfft are downloaded automatically during the first build and linked statically, so their development packages do not need to be installed separately.

## Online music library

Termusic can search a Subsonic-compatible server such as Navidrome, Gonic or
Airsonic. In **Core → General → Online music library**, configure:

- Enable Subsonic
- Server URL, such as `https://music.example.com` (without `/rest`)
- Online username and password
- Save online library

Then open **Agent** and enter `search song name` or `播放 歌名`. Results from
the online server are labelled `[Stream]`; an explicit play request starts the
best-ranked local or online result. HTTPS is recommended. The password stays in
the owner-only configuration file, while API requests use salted tokens.

## MPD playback backend

MPD is required for playback but is not a build dependency. If an MPD server is already available locally or remotely, skip the local setup below.

To connect to a remote server, run termusic with its address:

```bash
./build/termusic --host <MPD_HOST> --port <MPD_PORT>
```

You can also save the address under **Core → General → Host / Port → Save and reconnect**.

### Install MPD when no backend is available

Ubuntu / Debian:

```bash
sudo apt update
sudo apt install mpd
```

Fedora:

```bash
sudo dnf install mpd
```

Other downloads are available from the [official MPD download page](https://www.musicpd.org/download.html).

### Configure a user MPD service

This guide uses the user service and `$HOME/Music`. A system MPD service normally uses `/etc/mpd.conf` and `/var/lib/mpd/music`; stop it first so it does not occupy port 6600:

```bash
sudo systemctl disable --now mpd.service mpd.socket
systemctl --user disable --now mpd.socket
```

Create the music, playlist, data, and configuration directories:

```bash
mkdir -p "$HOME/Music" "$HOME/.config/mpd" "$HOME/.local/share/mpd/playlists"
```

Create `$HOME/.config/mpd/mpd.conf` with this content:

```conf
music_directory    "~/Music"
playlist_directory "~/.local/share/mpd/playlists"
db_file            "~/.local/share/mpd/database"
log_file           "~/.local/share/mpd/log"
pid_file           "~/.local/share/mpd/pid"
state_file         "~/.local/share/mpd/state"
sticker_file       "~/.local/share/mpd/sticker.sql"

bind_to_address "127.0.0.1"
port            "6600"
auto_update     "yes"

audio_output {
    type       "pulse"
    name       "Desktop audio"
    mixer_type "software"
}

# Required by the visualizer when MPD and termusic run on the same machine.
audio_output {
    type   "fifo"
    name   "termusic visualizer"
    path   "/tmp/mpd.fifo"
    format "44100:16:2"
}
```

The first output provides sound and volume control. If a track is playing but there is no sound or its volume stays at zero, keep `mixer_type "software"`. The configured `pulse` output works with PulseAudio and PipeWire systems that provide `pipewire-pulse`; otherwise change only its `type` to `pipewire` or `alsa`.

The second output sends audio data to termusic's visualizer. Its path and format must remain `/tmp/mpd.fifo` and `44100:16:2`, matching termusic's defaults. Without this output, music can play normally but the visualizer remains empty.

For a remote MPD server, this FIFO is created on the remote machine, not on the computer running termusic. The MPD network connection provides playback control and library data but does not carry the visualization audio stream, so the visualizer remains empty unless that PCM stream is forwarded separately to the local `/tmp/mpd.fifo`.

### Start MPD

Start MPD now and automatically after future logins:

```bash
systemctl --user enable --now mpd
systemctl --user status mpd --no-pager
```

After changing `$HOME/.config/mpd/mpd.conf`, restart MPD:

```bash
systemctl --user restart mpd
```

Reconnect termusic, open the Now Playing view with `i`, and press `k` to raise the volume.

If the user service is unavailable, start MPD directly:

```bash
mpd "$HOME/.config/mpd/mpd.conf"
```

Copy or move at least one supported audio file into `$HOME/Music`:

```bash
cp /path/to/your/song.mp3 "$HOME/Music/"
```

Start termusic, then select **Core → General → Update database**. When the update finishes, the songs will appear in **Library** and **Default**.

### Default playlist

**Default** lists all songs in the MPD media library and is read-only. Create another playlist when you need to add, remove, or reorder songs.

## Clone

```bash
git clone https://github.com/magicptr/termusic.git
```

## Build

```bash
cd termusic
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j
```

## Run

```bash
./build/termusic
```

termusic connects to `127.0.0.1:6600` by default.

## Install

Install system-wide:

```bash
sudo cmake --install build
termusic
```

Install for the current user only:

```bash
cmake --install build --prefix "$HOME/.local"
termusic
```

If `$HOME/.local/bin` is not in your `PATH`, run termusic directly:

```bash
"$HOME/.local/bin/termusic"
```

## Keyboard Shortcuts

### Global

| Shortcut | Action |
| --- | --- |
| `q` | Quit |
| `Esc` | Cancel or go back |
| `Space` | Play or pause |
| `r` | Toggle repeat |
| `s` | Toggle shuffle |
| `1` | Switch to the music library (Vault) |
| `2` | Switch to settings (Core) |
| `i` | Enter or leave the Now Playing view |
| `L` | Open or close lyrics for the current track |

### Navigation

| Shortcut | Action |
| --- | --- |
| `j` / `k` | Move down or up |
| `h` / `l` | Return to the left pane or enter the right pane |
| `Enter` | Open, confirm, or play the selected track |
| `g g` / `G` | Jump to the first or last item |
| `PageDown` / `PageUp` | Move down or up by one page |
| `Ctrl+d` / `Ctrl+u` | Move down or up by one page in the track list |

### Library and Playlists

| Shortcut | Action |
| --- | --- |
| `/` | Search the current list, or enter a new request in Agent |
| `n` / `N` | Jump to the next or previous search result |
| `a` | Create a playlist, or add a URL while viewing Streams |
| `r` | Rename the selected playlist |
| `d d` | Delete the selected track or playlist |
| `y y` | Copy the selected track to the register |
| `p` | Paste tracks from the register into a playlist |
| `v` | Enter visual selection mode |
| `K` / `J` | Move a track up or down in the playback queue |

In visual selection mode, use `j` and `k` to extend the selection, press `y` to copy, `d` to delete, or `Esc` to exit.

### Now Playing View

| Shortcut | Action |
| --- | --- |
| `h` / `l` | Previous or next track |
| `j` / `k` | Decrease or increase the volume |
| `,` / `.` | Seek backward or forward |
| `Space` | Play or pause |
| `i` / `Esc` | Leave the Now Playing view |

Except for the reserved quit key `q`, shortcuts can be changed under **Core → Keybindings**. Press `R` on that page to restore all default bindings.

---

termusic is still under active development. If you encounter a problem or have a suggestion, please [contact me](mailto:yiwithming@gmail.com).
