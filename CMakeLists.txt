cmake_minimum_required(VERSION 3.20)

project(termusic VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# FTXUI 7.0.3 is the release termusic is developed and tested against, and the
# build uses exactly that version on every path.
#
# A source build must not require a package manager. When a system package
# provides ftxuiConfig.cmake for 7.0.3, `find_package` uses it; any other system
# version (and no system package at all) falls back to FetchContent, which
# clones the pinned upstream release into the build tree. The tag is never a
# moving branch, so a rebuild reuses the already-fetched source instead of
# downloading it again.
#
# EXACT is what makes "the system package" mean 7.0.3 and not, say, 7.1 or 8.0:
# the generated ftxui-config-version.cmake sets PACKAGE_VERSION_EXACT only when
# the requested version string matches its own.
set(TERMUSIC_FTXUI_VERSION 7.0.3)
find_package(ftxui ${TERMUSIC_FTXUI_VERSION} EXACT CONFIG QUIET)
if(ftxui_FOUND)
  message(STATUS "FTXUI: using the system package ${ftxui_VERSION} (${ftxui_DIR})")
else()
  include(FetchContent)
  # The FTXUI test suite, examples and documentation are not part of termusic,
  # and neither are its install rules: `cmake --install` installs termusic and
  # its documents, never a third-party header, archive or CMake package file.
  set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v${TERMUSIC_FTXUI_VERSION}
    GIT_SHALLOW TRUE
  )
  message(STATUS "FTXUI: no system package for ${TERMUSIC_FTXUI_VERSION}, fetching v${TERMUSIC_FTXUI_VERSION}")
  FetchContent_MakeAvailable(ftxui)
endif()

# The remaining third-party components are built from pinned sources and linked
# statically, so a released binary needs no package for any of them: the
# spectrum FFT (cmake/Kissfft.cmake) and the MPD client library
# (cmake/Libmpdclient.cmake). FTXUI is resolved above (system 7.0.3 or the
# pinned FetchContent release).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Kissfft.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Libmpdclient.cmake)

add_library(termusic_core STATIC
  src/app/actions.cpp
  src/app/diagnostics.cpp
  src/app/reconnect.cpp
  src/app/history.cpp
  src/app/playback.cpp
  src/app/keymap.cpp
  src/app/state.cpp
  src/config/config.cpp
  src/config/paths.cpp
  src/ui/metrics.cpp
  src/ui/theme.cpp
  src/ui/widgets.cpp
  src/ui/visualizer/disc.cpp
  src/ui/visualizer/palette.cpp
  src/ui/visualizer/spectrum.cpp
  src/util/text.cpp
  src/visualizer/analyzer.cpp
  src/visualizer/beat.cpp
)

target_include_directories(termusic_core PUBLIC src)
target_link_libraries(termusic_core PUBLIC
  ftxui::screen
  ftxui::dom
  ftxui::component
  kissfft::kissfft
)

add_library(termusic_runtime STATIC
  src/backend/mpd_backend.cpp
  src/controller/controller.cpp
  src/ui/app.cpp
  # `core` is a module tree, not one file: every entry under the tree's second
  # root is its own translation unit with one interface.
  src/ui/core/about.cpp
  src/ui/core/appearance.cpp
  src/ui/core/general.cpp
  src/ui/core/help.cpp
  src/ui/core/keybindings.cpp
  src/ui/core/panel.cpp
  src/ui/core/panel_common.cpp
  src/ui/core/settings.cpp
)
target_link_libraries(termusic_runtime PUBLIC termusic_core termusic_mpdclient
                                             kissfft::kissfft)
# The bundled libmpdclient is built by an ExternalProject during the build, so
# the targets that use it must wait for it.
add_dependencies(termusic_runtime termusic_libmpdclient_project)

include(GNUInstallDirs)

add_executable(termusic src/main.cpp)
target_link_libraries(termusic PRIVATE termusic_runtime)
# ONE version source: the project version above. `--version`, the `--help`
# banner and the About page all read it from here, so a version bump is a
# one-line change.
# The version string is needed by anything that reports it (the `--version`
# banner, the About page), so it is defined for the libraries as well as the
# executable: a definition on `termusic` alone left other translation units
# compiling the "unknown" fallback.
target_compile_definitions(termusic_core PUBLIC
  TERMUSIC_VERSION="${PROJECT_VERSION}")

function(termusic_enable_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
  endif()
endfunction()
termusic_enable_warnings(termusic_core)
termusic_enable_warnings(termusic_runtime)
termusic_enable_warnings(termusic)

# A conventional installation: ${prefix}/bin/termusic plus the documents -- the
# same set a distributor packages from a source checkout.
# Nothing else is installed: no tests, no fixtures, no build files. Runtime
# resources are resolved through XDG (see src/config/paths.cpp), so the binary
# carries no dependency on this source tree.
install(TARGETS termusic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md LICENSE THIRD_PARTY_LICENSES.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
# The reference configuration: the same document --print-default-config prints.
install(FILES packaging/config.example.toml
        DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME config.example.toml)

# --- Uninstall ---------------------------------------------------------------
#
# `cmake --build <build-dir> --target uninstall` removes exactly the files that
# `cmake --install` recorded in THIS build directory's install_manifest.txt, and
# nothing else: the prefix, other applications' files in it and every directory
# are left alone. The script is generated rather than written by hand so that it
# always reads the manifest of the build tree it is used from -- and it refuses
# to run at all when that manifest does not exist, instead of guessing where an
# install might have gone.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
  @ONLY)
add_custom_target(uninstall
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
  COMMENT "Uninstalling the files recorded in install_manifest.txt")

include(CTest)
if(BUILD_TESTING)
  # The tests use a hook that deliberately breaks an internal invariant, and it
  # must exist in EVERY build that links them -- including a Release build,
  # where the library itself is compiled with NDEBUG. The switch is separate
  # from NDEBUG precisely so enabling the hook does not also enable the
  # analyzer's own assertions in a release binary.
  target_compile_definitions(termusic_core PUBLIC TERMUSIC_TEST_HOOKS)

  add_executable(termusic_tests
    tests/core_tests.cpp
  )
  target_link_libraries(termusic_tests PRIVATE termusic_core)
  # tests/core_tests.cpp uses setenv/unsetenv for the XDG resolution checks.
  target_compile_definitions(termusic_tests PRIVATE _POSIX_C_SOURCE=200809L)
  termusic_enable_warnings(termusic_tests)
  if(MSVC)
    target_compile_options(termusic_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(termusic_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME termusic_core COMMAND termusic_tests)

  # The playback-context decisions, against small fakes: the Controller needs a
  # backend and the backend needs MPD, but the rules do not.
  add_executable(termusic_context_tests tests/context_stub.cpp)
  target_link_libraries(termusic_context_tests PRIVATE termusic_core)
  termusic_enable_warnings(termusic_context_tests)
  if(MSVC)
    target_compile_options(termusic_context_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(termusic_context_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME termusic_playback_context COMMAND termusic_context_tests)

  # The immersive visualizer's geometry: centered grid, drawn size, and the
  # amplitude -> rectangle mapping. Pure functions, so no terminal is needed.
  add_executable(termusic_visualizer_layout_tests tests/visualizer_layout_test.cpp)
  target_link_libraries(termusic_visualizer_layout_tests PRIVATE termusic_core)
  termusic_enable_warnings(termusic_visualizer_layout_tests)
  if(MSVC)
    target_compile_options(termusic_visualizer_layout_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(termusic_visualizer_layout_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME termusic_visualizer_layout COMMAND termusic_visualizer_layout_tests)

  # The visualizer's shared layers: beat/onset timing, the palette registry, and
  # the SPECTRUM renderer itself -- its centred baseline, its absolute-height
  # gradient, its reflection and its runtime state (bounded, cleared by reset,
  # emptied by silence), measured on real rendered cells.
  add_executable(termusic_visualizer_tests tests/visualizer_render_test.cpp)
  target_link_libraries(termusic_visualizer_tests PRIVATE termusic_core)
  termusic_enable_warnings(termusic_visualizer_tests)
  if(MSVC)
    target_compile_options(termusic_visualizer_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(termusic_visualizer_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME termusic_visualizer
           COMMAND termusic_visualizer_tests)

  add_executable(termusic_disc_tests tests/disc_render_test.cpp)
  target_link_libraries(termusic_disc_tests PRIVATE termusic_core)
  termusic_enable_warnings(termusic_disc_tests)
  add_test(NAME termusic_disc COMMAND termusic_disc_tests)

  add_test(NAME termusic_disc_ui
           COMMAND termusic --demo
                   --ui-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/disc.ui)
  set_tests_properties(termusic_disc_ui PROPERTIES
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                       ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/disc-ui-home;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/disc-ui-home/config;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/disc-ui-home/data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/disc-ui-home/cache")

  # The immersive renderer, driven through its own UI-script harness: it builds
  # a synthetic 96-band spectrum, opens the now-playing view at three sizes and
  # prints the frames. The point is survival -- an index past the end of the
  # spectrum aborts the process in a build with library assertions on, which is
  # what a debug build is, so a regression here fails the suite instead of
  # silently rendering garbage in release.
  add_test(NAME termusic_immersive_render
           COMMAND termusic --ui-visualizer-motion-test
                   # An intentionally unused port: this test renders a
                   # SYNTHETIC spectrum and must never connect to (or disturb) a
                   # real MPD, least of all the user's own on 6600.
                   --host 127.0.0.1 --port 6699
                   --ui-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/immersive_render.ui)
  # Isolated HOME/XDG: the test runs the real binary, and it must not read or
  # write the user's configuration, history or logs.
  set_tests_properties(termusic_immersive_render PROPERTIES
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                       ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/config;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/cache")
endif()

# --- Distribution ------------------------------------------------------------
#
# termusic is distributed as SOURCE: `git clone`, configure, build, run, and
# optionally `cmake --install`. There is no CPack configuration and no binary
# package recipe here on purpose -- the install rules above are the only
# "package" definition, and a distributor who wants a .deb, an .rpm or an
# archive builds one from a source checkout with their own tooling.
#
# The version lives in `project(...)` alone; nothing in this file repeats it.
