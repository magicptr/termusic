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
- Access to an MPD server

Ubuntu / Debian:

```bash
sudo apt update
sudo apt install build-essential cmake git meson ninja-build
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake git meson ninja-build
```

FTXUI, libmpdclient, and kissfft are downloaded automatically during the first build and linked statically, so their development packages do not need to be installed separately.
If you do not have access to a remote MPD server, install and configure `mpd` locally.

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
