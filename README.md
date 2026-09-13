# termusic

termusic is a keyboard-driven terminal music client for
[MPD](https://www.musicpd.org/) (the Music Player Daemon). It is a client only:
MPD owns playback, the decoder, the audio output, the database and the queue,
while termusic provides the terminal interface over the MPD protocol.

## Features

- Browse the **Library** (the MPD database), your **History** and your saved
  **PlayLists**
- Control playback: play/pause, next/previous, seek, volume, shuffle, repeat
- A **spectrum visualizer** in the immersive now-playing view
- **Search** that filters the track list on screen
- Seven built-in **themes**
- Keybindings configurable in **Core → Keybindings**
- Native **plugins** (see [docs/extensions.md](docs/extensions.md))
- Two roots — Vault for your music, Core for the settings — fully usable over
  SSH

## Requirements

### Runtime

- Linux
- An **MPD server**, local or remote. termusic does not replace MPD: an MPD
  server must be installed and running for termusic to play anything. termusic
  never installs, starts, stops or configures the daemon — that lifecycle
  belongs to you or to your system. It connects to `127.0.0.1:6600` unless
  another endpoint is configured (Core → General, `config.toml`, `MPD_HOST` /
  `MPD_PORT`, or `--host` / `--port`).

Nothing else is needed at run time: no third-party shared library is linked
apart from the C++ runtime that ships with your distribution (`libstdc++`,
`libgcc_s`, `libm`, `libc`).

### Build

- CMake ≥ 3.20
- A C++20 compiler (GCC or Clang)
- Git
- Meson and Ninja — the bundled libmpdclient is built with Meson, whose build
  backend is Ninja
- A network connection for the first configure: FTXUI, libmpdclient and kissfft
  are fetched from their upstream repositories and built from source

You do **not** need development packages for FTXUI, libmpdclient or kissfft,
and no FFTW: CMake builds those three itself. No package manager and no vcpkg is
involved.

## Install build dependencies

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install build-essential cmake git meson ninja-build
```

### Fedora

```bash
sudo dnf install gcc-c++ make cmake git meson ninja-build
```

On Debian and Ubuntu `build-essential` brings the compilers and `make` with it;
on Fedora the compiler and `make` are their own packages (`gcc-c++`, `make`).
Meson, Ninja, CMake and Git are the only other tools the build asks for.

## Build and Run

With the build dependencies installed:

```bash
git clone https://github.com/magicptr/termusic.git
cd termusic

cmake -S . -B build
cmake --build build -j
```

The executable is written to `build/termusic`:

```bash
./build/termusic --version     # termusic 0.2.0
./build/termusic               # start with the configured MPD endpoint
```

A tagged source snapshot can be built the same way:

```bash
git checkout v0.2.0
```

## Install

`cmake --install` copies the binary, the plugin header and the documents. The
prefix is `CMAKE_INSTALL_PREFIX`, `/usr/local` by default, so a system-wide
install is:

```bash
sudo cmake --install build
termusic
```

To install into your own prefix instead — no `sudo`, nothing outside your home
directory:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j
cmake --install build
$HOME/.local/bin/termusic
```

`$HOME/.local/bin` is already in the `PATH` of most distributions. If it is not
in yours, add it in the startup file of the shell you use — for example, for a
Bourne-style shell:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

The installed files are:

```
${prefix}/bin/termusic
${prefix}/include/termusic/plugin_api.h
${prefix}/share/doc/termusic/README.md
${prefix}/share/doc/termusic/LICENSE
${prefix}/share/doc/termusic/THIRD_PARTY_LICENSES.md
${prefix}/share/doc/termusic/config.example.toml
```

## Uninstall

`cmake --build build --target uninstall` removes exactly the files that
`cmake --install` recorded in that build directory, and nothing else. Use `sudo`
only if you installed with `sudo`:

```bash
sudo cmake --build build --target uninstall    # after a system install
cmake --build build --target uninstall         # after a $HOME/.local install
```

The uninstaller works from `install_manifest.txt`, which lives in the build
directory: it can only undo an install made from that same directory. If the
build directory has been deleted, the record of what was installed is gone with
it — recreate the build directory, install again, and then uninstall. Nothing in
the prefix is searched or guessed at, and installed directories are left in
place even when they become empty.

## Usage

```bash
termusic                       # or ./build/termusic before installing
termusic --help                # every command-line option
termusic --version             # termusic 0.2.0
termusic --print-default-config > ~/.config/termusic/config.toml
```

The interface has two roots:

- **Vault** (`1`) — your music: the Library, History and your saved PlayLists.
- **Core** (`2`) — the settings: General, Appearance, Keybindings, Plugins,
  About and Help.

The left column is the tree, the right column is the content. `j` / `k` move
inside the focused column, `l` enters the right one and `h` goes back. `Enter`
opens a collection or plays the highlighted track. `i` opens the immersive
now-playing view: title, artist, the visualizer and the player bar.

## Keyboard shortcuts

Most shortcuts can be customized in **Core → Keybindings**; the tables below are
the shipped defaults.

### Global

| Key | Action |
|---|---|
| `q` | Quit (reserved: cannot be rebound) |
| `Esc` | Cancel / close |
| `Space` | Play / pause |
| `r` | Toggle repeat |
| `s` | Toggle shuffle |
| `1` / `2` | Go to the Vault root / the Core root |
| `i` | Toggle the immersive now-playing view |

### Vault tree

| Key | Action |
|---|---|
| `j` / `k` | Move down / up |
| `l` / `h` | Open the collection in the right column / back to the tree |
| `g g` / `G` | First / last row |
| `PageDown` / `PageUp` | Half page down / up |
| `Enter` | Expand a group, open a collection |
| `/` | Search the track list |
| `a` | Create a playlist |
| `r` | Rename the highlighted playlist |
| `d d` | Delete the highlighted playlist |
| `p` | Paste the register into a playlist |

### Track list

| Key | Action |
|---|---|
| `j` / `k` | Move down / up |
| `h` | Back to the tree |
| `g g` / `G` | First / last row |
| `Ctrl+d` / `Ctrl+u`, `PageDown` / `PageUp` | Half page down / up |
| `Enter` | Play the highlighted track |
| `/` | Search this list |
| `n` / `N` | Next / previous match |
| `v` | Visual selection |
| `y y` | Yank to the register |
| `d d` | Delete from the playlist |
| `J` / `K` | Move the item in MPD's queue |
| `a` | Create a playlist |

### Immersive now-playing

| Key | Action |
|---|---|
| `i` / `Esc` | Leave the immersive view |
| `Space` | Play / pause |
| `h` / `l` | Previous / next track |
| `j` / `k` | Volume down / up |
| `,` / `.` | Seek backward / forward |

### Settings (Core)

| Key | Action |
|---|---|
| `j` / `k` | Move down / up |
| `l` / `h` | Enter the settings pane / back to the tree |
| `Enter` | Change the highlighted setting |
| `R` | Restore all default keybindings |

## Search

`/` opens the box at the bottom and filters the **track list** of the collection
on screen — the Library, History or a saved playlist — so only the matching rows
stay visible; the tree is never searched or moved. `j` / `k` (or Up / Down) walk
the results, `Enter` closes the box, restores the full list and puts the cursor
on the chosen track, and `Esc` cancels. After a search, `n` / `N` walk the
accepted match in the full list.
cd
## Themes

Seven themes ship with termusic: Catppuccin Mocha (the default), Kanagawa,
Material Palenight, Monokai Pro, GitHub Dark, Oxocarbon and Catppuccin
Macchiato. Choose one in **Core → Appearance**; user themes placed in the
configured theme directory appear in the same list.

## License

GPL-3.0-or-later — see [LICENSE](LICENSE). The statically linked third-party
components and their notices are listed in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
