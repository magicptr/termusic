# termusic

Termusic is a lightweight, keyboard-driven terminal music player focused on local music playback. It uses Vim-style keyboard shortcuts for efficient music selection and playback.It is designed for Linux users, terminal enthusiasts, Vim users, and anyone who wants a
fast, distraction-free way to manage and play a local music library without leaving the command line.

## Demo

![Library view](./docs/images/list.png)
![Now Playing view](./docs/images/play.png)

## Requirements

### Runtime

- Linux
- An **MPD server** for playback, local or remote. termusic is a client: it never
  installs, starts or configures the daemon. See
  [MPD playback backend](#mpd-playback-backend) if you do not have one yet.
- Building and running `--help` / `--version` need **no** MPD at all, so a fresh
  machine can be set up in the order below without touching MPD first.

### Build tools

- A C++20-compatible compiler (GCC or Clang)
- CMake 3.20 or later
- Git (also used to fetch the dependencies)
- Meson
- Ninja (Meson's build backend)

FTXUI, libmpdclient, and kissfft are downloaded automatically during the first
build and linked statically, so their development packages do not need to be
installed separately. That first configure needs network access to their
upstream repositories; later builds reuse what was already downloaded. No
package manager, no `pkg-config`, and no vcpkg is involved.

## Install the build tools

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install build-essential cmake git meson ninja-build
```

### Fedora

```bash
sudo dnf install gcc-c++ make cmake git meson ninja-build
```

### Arch Linux

```bash
sudo pacman -S --needed base-devel git cmake meson ninja
```

### Any other distribution

Install the equivalents: a C++20 compiler, CMake 3.20 or newer, Git, Meson and
Ninja. `make` is worth having too, in case you configure without `-G Ninja` and
CMake picks its default generator.

Check the tools once before building — the CMake floor is the one that bites:

```bash
cmake --version     # must be 3.20 or newer
meson --version
ninja --version
git --version
c++ --version       # or clang++ --version
```

If your distribution only ships an older CMake, install a newer one from
[cmake.org](https://cmake.org/download/) or your distribution's backports
before continuing.

With the tools in place, continue with [Clone](#clone) and [Build](#build). MPD
is only needed to play something, so the
[MPD playback backend](#mpd-playback-backend) section can wait until after
termusic starts.

## MPD playback backend

MPD is required for playback but is not a build dependency. If an MPD server is already available locally or remotely, skip the local setup below.

To connect to a remote server, run termusic with its address:

```bash
./build/termusic --host 192.168.1.20 --port 6600    # your MPD server's address
```

The values must be replaced: `--port` accepts digits only, so pasting a
placeholder such as `<MPD_PORT>` prints `--port expects an integer from 1 to
65535` and exits.

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

The first configure downloads FTXUI, libmpdclient and kissfft and builds
libmpdclient, so it needs network access and takes a few minutes; after that a
rebuild works offline.

```bash
cd termusic
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build
```

Ninja already builds with all cores; add `--parallel <N>` to `cmake --build` if
you want to cap the number of jobs. Do not write `-j"$(nproc)"`: wherever the
`$(...)` is not expanded by a shell (a Makefile, a CI step, fish), CMake gets the
literal text and answers `'-j' invalid number '$(nproc)' given.`

`-DBUILD_TESTING=OFF` skips the test binaries. To build and run them as well,
configure with `-DBUILD_TESTING=ON` instead and finish with
`ctest --test-dir build --output-on-failure`.

`-G Ninja` pins the generator. If `build/` already exists from a configure that
used a different one, CMake refuses to reuse it (`does not match the generator
used previously`): delete that directory or configure into a fresh one
(`-B build-ninja`).

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

Install for the current user only — reconfigure the build directory with your
own prefix first:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
$HOME/.local/bin/termusic
```

If `$HOME/.local/bin` is not in your `PATH`, add it — for a Bourne-style shell:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

## Uninstall

Remove a system-wide installation:

```bash
sudo cmake --build build --target uninstall
```

Remove an installation made for the current user:

```bash
cmake --build build --target uninstall
```

Run the uninstall command from the same build directory that was used by
`cmake --install`. The generated install manifest records the exact files and
prefix used by that installation, so uninstall removes only termusic's files
and leaves shared directories such as `/usr/local/bin` and `$HOME/.local/bin`
in place.

## Keyboard Shortcuts

### Global

| Shortcut | Action |
| --- | --- |
| `q` | Quit |
| `Esc` | Cancel or go back |
| `Space` | Play or pause |
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
| `r` | Toggle repeat |
| `Space` | Play or pause |
| `i` / `Esc` | Leave the Now Playing view |

Except for the reserved quit key `q`, shortcuts can be changed under **Core → Keybindings**. Press `R` on that page to restore all default bindings.

---
