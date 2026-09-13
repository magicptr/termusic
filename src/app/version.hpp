#pragma once

#include <string_view>

/// ONE canonical version for the whole program.
///
/// The value is injected by CMake from `project(termusic VERSION ...)`, so
/// `--version`, the `--help` banner and the About page all derive from the same
/// number. The fallback exists only so a translation unit can be compiled
/// outside CMake (a syntax check, an editor's clangd run) without lying about a
/// version it does not know.
#ifndef TERMUSIC_VERSION
#define TERMUSIC_VERSION "0.0.0-unknown"
#endif

namespace termusic {

/// e.g. "0.2.0". Never contains a path, a commit or a build type: the version
/// line is stable and parseable.
inline constexpr std::string_view kVersion = TERMUSIC_VERSION;

/// "termusic 0.2.0" -- the one line `--version` prints.
inline constexpr std::string_view kVersionLine = "termusic " TERMUSIC_VERSION;

} // namespace termusic
