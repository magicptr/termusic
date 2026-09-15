# termusic

termusic is a lightweight, keyboard-driven terminal music client for MPD (Music Player Daemon). It lets you browse your music library, control playback, search for tracks, manage playlists, switch themes, view playback history, load plugins, and display a spectrum visualizer. It can connect to an MPD server running locally or remotely.

## Demo

![Library view](./docs/images/list.png)
![Now Playing view](./docs/images/play.png)

## Requirements

- A C++20-compatible compiler
- CMake 3.20 or later
- Git
- Meson
- Ninja
- MPD (the playback backend), either on this computer or a remote server

For a complete local installation on Ubuntu / Debian:

```bash
sudo apt update
sudo apt install build-essential cmake git meson ninja-build mpd mpc
```

For a complete local installation on Fedora:

```bash
sudo dnf install gcc-c++ cmake git meson ninja-build mpd mpc
```

FTXUI, libmpdclient, and kissfft are downloaded automatically during the first build and linked statically, so their development packages do not need to be installed separately.

If you already use an MPD server on another computer, `mpd` and `mpc` are not required locally. Packages and source releases for other platforms are available from the [official MPD download page](https://www.musicpd.org/download.html).

## Set up a local MPD backend

The following setup runs MPD as your own user, so it can read music in your home directory and use your desktop audio session. First, stop the distribution's system-wide MPD service if it was started automatically:

```bash
sudo systemctl disable --now mpd.service mpd.socket 2>/dev/null || true
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
```

When no `audio_output` is specified, MPD automatically selects an available PipeWire, PulseAudio, or ALSA output. Start MPD now and automatically after future logins:

```bash
systemctl --user enable --now mpd
```

If your distribution does not provide the user service, start the daemon directly instead:

```bash
mpd "$HOME/.config/mpd/mpd.conf"
```

Copy or move at least one supported audio file into `$HOME/Music`, update the database, and verify the backend:

```bash
cp /path/to/your/song.mp3 "$HOME/Music/"
mpc update
mpc status
mpc outputs
```

`mpc status` should connect without an error, and `mpc outputs` should show at least one enabled output. The local backend is now ready at `127.0.0.1:6600`.

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

termusic connects to `127.0.0.1:6600` by default. To connect to a remote MPD server, run:

```bash
./build/termusic --host <MPD_HOST> --port <MPD_PORT>
```

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
| `/` | Search the current list |
| `n` / `N` | Jump to the next or previous search result |
| `a` | Create a playlist |
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
