# termusic v0.2.0

A visual and structural pass over the whole interface, plus a build that no
longer needs a package manager.

## Highlights

- **One Spectrum visualizer.** The Classic Bars / Waterfall / Particles styles
  and the Style selector are gone. The Spectrum draws thin, independent bars —
  one cell of bar, one cell of air — with a vertical gradient keyed to the
  absolute screen height, a persistent dot baseline underneath, and a size-aware
  layout that spreads the bars across the width and keeps the peaks in the
  middle. The dots stay on screen when the music stops: a silent player shows
  the baseline, never a blank pane.
- **Seven built-in themes.** Catppuccin Mocha (default), Kanagawa, Material
  Palenight, Monokai Pro, GitHub Dark, Oxocarbon and Catppuccin Macchiato, all
  mapped onto the existing semantic roles. Nord was removed.
- **Appearance, reorganised.** Theme first, then a Display group where
  Visualizer is the active mode and Disc is a reserved, not-yet-available
  placeholder. The style selector and the old "Enabled" switch are gone.
- **Search filters the track list.** `/` now narrows the Library, History or
  playlist you are looking at to the matching rows only; choosing a result
  closes the box, restores the full list and moves the cursor to that exact
  row. The tree is never searched or moved.
- **A UTF-8 safe input path.** The search line is a persistent FTXUI input:
  editing, deletion and the caret work on codepoints, the terminal cursor is
  placed on the caret where an IME anchors its composition, and the visualizer
  no longer repaints while the box is open.
- **A self-contained build.** libmpdclient is bundled and linked statically,
  FFTW was replaced by kissfft, and the vcpkg requirement is gone: the normal
  build needs CMake, Ninja, Meson, git and a C++20 compiler, and the binary it
  produces depends on the platform runtime only (no `libmpdclient.so`, no
  `libfftw3.so`, no `libftxui*.so`).

## Distribution

termusic is distributed as source: clone the repository, configure and build it
with CMake, run it, and optionally `cmake --install` it. The tree carries no
binary package recipe -- no CPack configuration, no DEB/RPM metadata and no
workflow that publishes compiled assets. `v0.2.0` is a tag on the source, and
the version it names comes from `project(termusic VERSION ...)`.

The MPD daemon is not a dependency: termusic is a client, and a local `mpd` is
only useful for users who want a local server.

## Notes

- Disc is a placeholder for a future display mode. It is visible in Appearance
  and cannot be selected.
- The Analyzer, the FFT and the MPD backend are unchanged.
