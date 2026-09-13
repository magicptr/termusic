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

# FetchContent gained native EXCLUDE_FROM_ALL support in CMake 3.28. Use its
# recommended MakeAvailable API where possible, while keeping the project
# compatible with its CMake 3.20 minimum. The older branch is only reached on
# CMake versions where direct Populate is not deprecated.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
  FetchContent_Declare(kissfft
    GIT_REPOSITORY "${TERMUSIC_KISSFFT_REPOSITORY}"
    GIT_TAG "${TERMUSIC_KISSFFT_VERSION}"
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(kissfft)
else()
  FetchContent_Declare(kissfft
    GIT_REPOSITORY "${TERMUSIC_KISSFFT_REPOSITORY}"
    GIT_TAG "${TERMUSIC_KISSFFT_VERSION}"
    GIT_SHALLOW TRUE
  )
  FetchContent_Populate(kissfft)
  add_subdirectory("${kissfft_SOURCE_DIR}" "${kissfft_BINARY_DIR}"
                   EXCLUDE_FROM_ALL)
endif()

message(STATUS "kissfft: embedded ${TERMUSIC_KISSFFT_VERSION} "
               "(static, single precision)")
