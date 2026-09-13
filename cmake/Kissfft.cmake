# kissfft: the FFT behind the visualizer's spectrum analyzer.
#
# termusic used to link the system FFTW through pkg-config, which made
# libfftw3f.so.3 a runtime requirement of every released binary. kissfft is a
# small, permissively licensed (BSD-3-Clause) real-FFT implementation that is
# compiled into the executable instead, so the portable build needs no FFT
# package at all -- neither at build time nor at run time.
#
# The version is pinned to an upstream release tag, and the sources live in the
# generated build tree: they are never committed and never part of the release
# archive. Only the library itself is built (single precision, no tools, tests
# or pkg-config file), and its own install rules are EXCLUDED: `cmake --install`
# must install termusic and its documents, never a third-party header, archive
# or CMake package file.

set(TERMUSIC_KISSFFT_VERSION 131.2.0)
set(TERMUSIC_KISSFFT_REPOSITORY "https://github.com/mborgerding/kissfft.git")

include(FetchContent)

set(KISSFFT_DATATYPE "float" CACHE STRING "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)

FetchContent_Declare(kissfft
  GIT_REPOSITORY "${TERMUSIC_KISSFFT_REPOSITORY}"
  GIT_TAG "${TERMUSIC_KISSFFT_VERSION}"
  GIT_SHALLOW TRUE
)
# Populate + add_subdirectory(EXCLUDE_FROM_ALL) instead of MakeAvailable: the
# subdirectory's install rules are then ignored, which is what keeps the
# third-party headers and archives out of `cmake --install` and out of every
# package. (MakeAvailable would add it as an ordinary subdirectory, and its
# install() rules would run with ours.)
FetchContent_Populate(kissfft)
add_subdirectory("${kissfft_SOURCE_DIR}" "${kissfft_BINARY_DIR}" EXCLUDE_FROM_ALL)

message(STATUS "kissfft: embedded ${TERMUSIC_KISSFFT_VERSION} "
               "(static, single precision)")
