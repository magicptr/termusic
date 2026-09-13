# termusic

termusic is a keyboard-driven terminal music client for
[MPD](https://www.musicpd.org/).

It provides a terminal interface for browsing your music library, controlling
playback, searching tracks, managing playlists, changing themes and viewing a
spectrum visualizer.

## Dependencies

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install build-essential cmake git meson ninja-build mpd
```

### Fedora

```bash
sudo dnf install gcc-c++ make cmake git meson ninja-build mpd
```

## Build

Clone the repository:

```bash
git clone https://github.com/magicptr/termusic.git
cd termusic
```

Build:

```bash
cmake -S . -B build
cmake --build build -j
```

Run directly:

```bash
./build/termusic
```

## Install

Install system-wide:

```bash
sudo cmake --install build
```

Then start termusic from any terminal:

```bash
termusic
```

To install for the current user only:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j
cmake --install build
```

## Keyboard Shortcuts

### Global

| Key | Action |
|---|---|
| `q` | Quit |
| `Esc` | Cancel / Back |
| `Space` | Play / Pause |
| `1` | Vault |
| `2` | Core |
| `i` | Immersive view |
| `r` | Toggle repeat |
| `s` | Toggle shuffle |
| `/` | Search |

### Navigation

| Key | Action |
|---|---|
| `j` / `k` | Move down / up |
| `h` / `l` | Move left / right |
| `Enter` | Open / Play |
| `g g` / `G` | First / last item |
| `PageUp` / `PageDown` | Move by page |

### Track List

| Key | Action |
|---|---|
| `Enter` | Play selected track |
| `v` | Visual selection |
| `y y` | Yank |
| `d d` | Delete |
| `n` / `N` | Next / previous search result |

### Immersive View

| Key | Action |
|---|---|
| `Space` | Play / Pause |
| `h` / `l` | Previous / next track |
| `j` / `k` | Volume down / up |
| `,` / `.` | Seek backward / forward |
| `i` / `Esc` | Exit immersive view |

Most shortcuts can be changed in **Core → Keybindings**.

## Contact

If you have any problems while using termusic, please contact:

**33333@gmail.com**