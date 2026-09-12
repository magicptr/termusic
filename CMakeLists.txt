cmake_minimum_required(VERSION 3.20)

project(termusic VERSION 0.1.0 LANGUAGES CXX)

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
  # The FTXUI test suite, examples and documentation are not part of termusic.
  set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v${TERMUSIC_FTXUI_VERSION}
    GIT_SHALLOW TRUE
  )
  message(STATUS "FTXUI: no system package for ${TERMUSIC_FTXUI_VERSION}, fetching v${TERMUSIC_FTXUI_VERSION}")
  FetchContent_MakeAvailable(ftxui)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(MPDCLIENT REQUIRED IMPORTED_TARGET libmpdclient)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)

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
  src/extensions/extension_registry.cpp
  src/ui/metrics.cpp
  src/ui/theme.cpp
  src/ui/widgets.cpp
  src/ui/visualizer/cell_grid.cpp
  src/ui/visualizer/classic_bars.cpp
  src/ui/visualizer/palette.cpp
  src/ui/visualizer/particles.cpp
  src/ui/visualizer/registry.cpp
  src/ui/visualizer/waterfall.cpp
  src/util/text.cpp
  src/visualizer/analyzer.cpp
  src/visualizer/beat.cpp
)

target_include_directories(termusic_core PUBLIC src)
target_link_libraries(termusic_core PUBLIC
  ftxui::screen
  ftxui::dom
  ftxui::component
  PkgConfig::FFTW3F
)

add_library(termusic_runtime STATIC
  src/backend/mpd_backend.cpp
  src/controller/controller.cpp
  src/extensions/plugin_manager.cpp
  src/ui/app.cpp
  # `core` is a module tree, not one file: every entry under the tree's second
  # root is its own translation unit with one interface.
  src/ui/core/appearance.cpp
  src/ui/core/connection.cpp
  src/ui/core/general.cpp
  src/ui/core/help.cpp
  src/ui/core/keybindings.cpp
  src/ui/core/panel.cpp
  src/ui/core/panel_common.cpp
  src/ui/core/plugins.cpp
  src/ui/core/settings.cpp
)
target_link_libraries(termusic_runtime PUBLIC termusic_core PkgConfig::MPDCLIENT
                                             ${CMAKE_DL_LIBS})

include(GNUInstallDirs)

add_executable(termusic src/main.cpp)
target_link_libraries(termusic PRIVATE termusic_runtime)
# ONE version source: the project version above. `--version`, the `--help`
# banner, the release archive name and the package metadata all read it from
# here, so a release bump is a one-line change.
target_compile_definitions(termusic PRIVATE
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

# A conventional installation: ${prefix}/bin/termusic plus the plugin header and
# the documents. Nothing else belongs in a package -- no tests, no fixtures, no
# build files. Runtime resources are resolved through XDG (see src/config/
# paths.cpp), so the binary carries no dependency on this source tree.
install(TARGETS termusic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/extensions/plugin_api.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/termusic)
install(FILES README.md LICENSE THIRD_PARTY_LICENSES.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
# The reference configuration: the same document --print-default-config prints.
install(FILES packaging/config.example.toml
        DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME config.example.toml)
# termusic never installs or manages an MPD service; the packaging metadata only
# RECOMMENDS a local MPD. This file documents that contract for packagers.
install(FILES packaging/README.packaging
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

include(CTest)
if(BUILD_TESTING)
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

  # The visualizer's shared layers: beat/onset timing, the style and palette
  # registries, and the renderers' runtime state (bounded, cleared by reset,
  # emptied by silence).
  add_executable(termusic_visualizer_styles_tests tests/visualizer_styles_test.cpp)
  target_link_libraries(termusic_visualizer_styles_tests PRIVATE termusic_core)
  termusic_enable_warnings(termusic_visualizer_styles_tests)
  if(MSVC)
    target_compile_options(termusic_visualizer_styles_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(termusic_visualizer_styles_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME termusic_visualizer_styles
           COMMAND termusic_visualizer_styles_tests)

  add_library(termusic_test_plugin MODULE tests/fixture_plugin.cpp)
  target_include_directories(termusic_test_plugin PRIVATE src)
  set_target_properties(termusic_test_plugin PROPERTIES PREFIX "")

  add_executable(termusic_plugin_tests tests/plugin_tests.cpp)
  target_link_libraries(termusic_plugin_tests PRIVATE termusic_runtime)
  termusic_enable_warnings(termusic_plugin_tests)
  if(MSVC)
    target_compile_options(termusic_plugin_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(termusic_plugin_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME termusic_plugins COMMAND termusic_plugin_tests
           $<TARGET_FILE:termusic_test_plugin>)

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
  # write the user's configuration, history, plugins or logs.
  set_tests_properties(termusic_immersive_render PROPERTIES
                       WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                       ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/config;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/cache")
endif()
