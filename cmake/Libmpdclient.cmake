# libmpdclient for termusic: project-controlled and statically linked.
#
# The official build must not depend on whichever libmpdclient happens to be
# installed on the machine that builds it, so this module fetches the pinned
# upstream release and builds it with its own (Meson) build system into a
# private prefix inside the build tree. The resulting static library is linked
# into the termusic executable, which is what makes the released binary portable
# across distributions.
#
# `-DTERMUSIC_USE_SYSTEM_LIBMPDCLIENT=ON` is the explicit developer mode: it
# links the system package through pkg-config instead. That binary needs
# libmpdclient.so.2 at run time, so it is never used for a release.
#
# Everything here is C and platform-independent; the only platform-specific part
# is upstream's own Meson configuration.

set(TERMUSIC_LIBMPDCLIENT_VERSION 2.26)
set(TERMUSIC_LIBMPDCLIENT_REPOSITORY
    "https://github.com/MusicPlayerDaemon/libmpdclient.git")

option(TERMUSIC_USE_SYSTEM_LIBMPDCLIENT
  "Link the system libmpdclient instead of the pinned bundled one (developer builds only; the result is NOT portable)"
  OFF)

if(TERMUSIC_USE_SYSTEM_LIBMPDCLIENT)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(MPDCLIENT REQUIRED IMPORTED_TARGET libmpdclient)
  add_library(termusic_mpdclient INTERFACE)
  target_link_libraries(termusic_mpdclient INTERFACE PkgConfig::MPDCLIENT)
  message(STATUS "libmpdclient: system package via pkg-config (developer mode: "
                 "the binary needs libmpdclient.so.2 at run time)")
  return()
endif()

# --- the portable path: build the pinned release ourselves -------------------
find_program(TERMUSIC_GIT_EXECUTABLE git)
if(NOT TERMUSIC_GIT_EXECUTABLE)
  message(FATAL_ERROR
    "Fetching libmpdclient ${TERMUSIC_LIBMPDCLIENT_VERSION} needs git, which was not found.\n"
    "  * install git and network access to ${TERMUSIC_LIBMPDCLIENT_REPOSITORY}, or\n"
    "  * configure with -DTERMUSIC_USE_SYSTEM_LIBMPDCLIENT=ON to link a system libmpdclient.")
endif()

find_program(TERMUSIC_MESON_EXECUTABLE meson)
if(NOT TERMUSIC_MESON_EXECUTABLE)
  message(FATAL_ERROR
    "libmpdclient ${TERMUSIC_LIBMPDCLIENT_VERSION} is built with Meson, which was not found.\n"
    "  * install Meson and Ninja, or\n"
    "  * configure with -DTERMUSIC_USE_SYSTEM_LIBMPDCLIENT=ON to link a system libmpdclient.")
endif()

include(ExternalProject)

set(_termusic_deps_dir "${CMAKE_BINARY_DIR}/_deps")
set(_termusic_mpdclient_prefix "${_termusic_deps_dir}/libmpdclient-prefix")
set(_termusic_mpdclient_library
    "${_termusic_mpdclient_prefix}/lib/libmpdclient.a")

# The dependency follows the variant being built, so a debug termusic links a
# debug libmpdclient and a release termusic a release one -- always the same
# source version.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_termusic_mpdclient_buildtype "debug")
else()
  set(_termusic_mpdclient_buildtype "release")
endif()
# GIT_SHALLOW keeps the download small; UPDATE_COMMAND "" keeps a pinned tag
# pinned -- the dependency is fetched once and never updated behind the user's
# back. The install goes into the private prefix above, never into a system
# prefix.
#
# The build and install steps go through `meson compile` / `meson install`
# rather than through a build tool named here: Meson owns the backend it
# configured, so the dependency builds with whatever generator the USER chose
# for termusic -- the default `Unix Makefiles`, Ninja, or anything else.
# Invoking `${CMAKE_MAKE_PROGRAM}` instead would run `make -C <meson dir>`,
# which has no Makefile to read and fails.
ExternalProject_Add(termusic_libmpdclient_project
  PREFIX "${_termusic_deps_dir}/libmpdclient"
  GIT_REPOSITORY "${TERMUSIC_LIBMPDCLIENT_REPOSITORY}"
  GIT_TAG "v${TERMUSIC_LIBMPDCLIENT_VERSION}"
  GIT_SHALLOW TRUE
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND
    "${TERMUSIC_MESON_EXECUTABLE}" setup
      --prefix=<INSTALL_DIR>
      --libdir=lib
      --default-library=static
      --buildtype=${_termusic_mpdclient_buildtype}
      -Ddocumentation=false
      -Dtest=false
      <SOURCE_DIR> <BINARY_DIR>
  BUILD_COMMAND
    "${TERMUSIC_MESON_EXECUTABLE}" compile -C <BINARY_DIR>
  INSTALL_COMMAND
    "${TERMUSIC_MESON_EXECUTABLE}" install -C <BINARY_DIR>
  BUILD_BYPRODUCTS "${_termusic_mpdclient_library}"
  INSTALL_DIR "${_termusic_mpdclient_prefix}"
  LOG_CONFIGURE ON
  LOG_BUILD ON
  LOG_INSTALL ON
  COMMENT "Building bundled libmpdclient ${TERMUSIC_LIBMPDCLIENT_VERSION} (static)"
)

add_library(termusic_mpdclient STATIC IMPORTED GLOBAL)
# The include directory only receives its headers when the dependency is built,
# but CMake validates INTERFACE_INCLUDE_DIRECTORIES at generate time -- so the
# directory is created here and filled in later by the install step.
file(MAKE_DIRECTORY "${_termusic_mpdclient_prefix}/include")
file(MAKE_DIRECTORY "${_termusic_mpdclient_prefix}/lib")
set_target_properties(termusic_mpdclient PROPERTIES
  IMPORTED_LOCATION "${_termusic_mpdclient_library}"
  INTERFACE_INCLUDE_DIRECTORIES "${_termusic_mpdclient_prefix}/include")
add_dependencies(termusic_mpdclient termusic_libmpdclient_project)

message(STATUS "libmpdclient: bundled ${TERMUSIC_LIBMPDCLIENT_VERSION}, "
               "statically linked (${_termusic_mpdclient_prefix})")
