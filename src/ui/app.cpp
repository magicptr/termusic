#include "ui/app.hpp"

#include "app/reconnect.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <ctime>
#include <exception>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <istream>
#include <map>
#include <ostream>
#include <sstream>
#include <utility>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <fcntl.h>
#include <unistd.h>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "util/text.hpp"

namespace termusic::ui {

/// Splits text into one UTF-8 codepoint per string (defined next to
/// scriptKey()). Declared here because the `type` script command uses it.
std::vector<std::string> scriptCodepoints(std::string_view text);

namespace {

using namespace ftxui;

/// The visualizer's drawing unit: one compact black square per cell, which
/// reads as a small rectangle with its own margin. Braille is deliberately NOT
/// used anywhere in the visualizer -- its 2x4 dot matrix is what made the old
/// spectrum look like a particle field.
constexpr const char *kVisualizerBlock = "\u25aa";
/// How long transient feedback stays on screen. Short on purpose: it replaced
/// the permanent status line, and a message that lingers is that line again.
constexpr int kToastMs = 2500;

/// `rows` blank lines: the logical separators of the immersive page.
///
/// Built as a stack of one-row elements on purpose. `text(repeat(n, " "))`
/// looks like it produces n rows and does not -- it produces ONE row that is n
/// cells wide, which is exactly how a "two row" gap silently becomes one row
/// and pushes the whole layout up.
ftxui::Element blankRows(int rows) {
  // Zero rows is ZERO rows. `text("")` is a one-row element, so returning it
  // here used to add a row that the layout had not budgeted -- enough, in the
  // sidebar, to push the playback block out of the region the metrics reserved
  // for it.
  if (rows <= 0)
    return emptyElement();
  Elements lines;
  lines.reserve(static_cast<std::size_t>(rows));
  for (int i = 0; i < rows; ++i)
    lines.push_back(text(" "));
  return vbox(std::move(lines));
}

int pageIndex(Page page) {
  const auto found = std::find(allPages().begin(), allPages().end(), page);
  return found == allPages().end()
             ? 0
             : static_cast<int>(std::distance(allPages().begin(), found));
}

} // namespace

Application::Application(AppState &state, Controller &controller,
                         MpdBackend &backend, VisualizerAnalyzer &analyzer,
                         ThemeRegistry &themes)
    : state_(state), controller_(controller), backend_(backend),
      analyzer_(analyzer), themes_(themes),
      theme_(themes.resolve(controller.config().theme_name)) {
  page_index_ = pageIndex(state_.page);
  configureKeymap();
  // Startup consistency: the Tree cursor and the Track Buffer both start on the
  // MEDIA DATABASE. It is the one collection that always exists -- there is no
  // synthetic queue row to open, and a user may have no saved playlists at all.
  active_collection_ = ActiveCollection::Library;
  active_playlist_name_.clear();
  active_playlist_index_ = -1;
  controller_.selectDatabase();
  for (std::size_t index = 0; index < workspace_tree_.visible().size(); ++index) {
    if (workspace_tree_.visible()[index].type != TreeNodeType::Database)
      continue;
    workspace_tree_.setCursor(static_cast<int>(index));
    break;
  }
  icon_set_ = iconSetFromName(controller_.config().icon_set);
  slider_renderer_ =
      sliderRendererFromName(controller_.config().slider_renderer);
  ui_slider_test_ = controller_.config().ui_slider_test;
  mouse_debug_ = controller_.config().ui_mouse_debug;
  motion_test_ = controller_.config().ui_visualizer_motion_test;
  ui_test_start_ = std::chrono::steady_clock::now();

  // The `core` module tree. Every hook the sections get is an explicit
  // capability: a section can change the theme, the connection or a binding
  // only through these, so no section can reach into the Application.
  // The capabilities the `core` modules are allowed to use. They live in
  // named members because CoreContext keeps REFERENCES to them: a reference
  // bound to a temporary here would leave the first section that used it
  // calling an empty std::function.
  core_start_page_index_ = [this] {
    return pageIndex(controller_.config().start_page);
  };
  core_set_start_page_index_ = [this](int index) {
    controller_.setStartPage(allPages()[static_cast<std::size_t>(index)]);
  };
  core_save_config_ = [this] { return controller_.saveConfig(); };
  core_theme_changed_ = [this] {
    theme_ = themes_.resolve(controller_.config().theme_name);
    screen_.PostEvent(Event::Custom);
  };
  core_visualizer_changed_ = [this] { configureVisualizer(); };
  core_update_database_ = [this] {
    controller_.execute(Action::UpdateDatabase);
  };
  core_config_path_ = [this] {
    return controller_.configPath().string();
  };
  core_content_focused_ = [this] {
    return workspace_pane_ == WorkspacePane::TrackList && !overlayOwnsKeyboard();
  };
  core_transient_message_ = [this]() -> const std::string & {
    return visual_message_;
  };
  core_preview_binding_ = [this](KeyContext context, Action action,
                                 const std::vector<std::string> &sequences,
                                 std::string *problem) {
    return keymap_.applyOverride(context, actionId(action), sequences, problem);
  };
  core_save_binding_ = [this](KeyContext context, Action action,
                              const std::vector<std::string> &sequences) {
    // An empty candidate list means "restore the built-in default", which is
    // the same call with the override removed.
    return sequences.empty() ? controller_.unbindKey(context, action)
                             : controller_.bindKey(context, action, sequences);
  };
  core_reset_bindings_ = [this] { controller_.resetKeymap(); };
  core_connection_changed_ = [this](std::string host, int port,
                                    std::string password) {
    controller_.setMpdConnection(std::move(host), port, std::move(password),
                                 controller_.config().auto_reconnect);
    if (state_.mpd_connected)
      startBackendEvents();
  };
  core_compact_button_ = [this] { return ui::compactButton(theme_); };

  core_context_ = std::make_unique<core::CoreContext>(core::CoreContext{
      .state = state_,
      .controller = controller_,
      .themes = themes_,
      .metrics = metrics_,
      .start_page_index = core_start_page_index_,
      .set_start_page_index = core_set_start_page_index_,
      .save_config = core_save_config_,
      .theme_changed = core_theme_changed_,
      .visualizer_changed = core_visualizer_changed_,
      .update_database = core_update_database_,
      .config_path = core_config_path_,
      .content_focused = core_content_focused_,
      .transient_message = core_transient_message_,
      .keymap = keymap_,
      .preview_binding = core_preview_binding_,
      .save_binding = core_save_binding_,
      .reset_bindings = core_reset_bindings_,
      .connection_changed = core_connection_changed_,
      .compact_button = core_compact_button_,
  });
  buildComponents();
}

Application::~Application() { stopWorkers(); }

void Application::printDiagnostics() const {
  // Terminal geometry as the kernel reports it.
  struct winsize size {};
  const bool have_size =
      ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0;
  std::fprintf(stderr, "termusic diagnostics\n");
  std::fprintf(stderr, "  terminal cells   : %dx%d\n",
               have_size ? size.ws_col : -1, have_size ? size.ws_row : -1);
  if (have_size && size.ws_xpixel > 0 && size.ws_ypixel > 0) {
    std::fprintf(stderr, "  cell pixel size  : %.2f x %.2f px\n",
                 static_cast<double>(size.ws_xpixel) / size.ws_col,
                 static_cast<double>(size.ws_ypixel) / size.ws_row);
  } else {
    std::fprintf(stderr,
                 "  cell pixel size  : not reported by this terminal; px "
                 "ratios below are estimates (~10 x 19 px assumed)\n");
  }
  const char *term = std::getenv("TERM");
  const char *colorterm = std::getenv("COLORTERM");
  std::fprintf(stderr, "  TERM/COLORTERM   : %s / %s\n",
               term ? term : "(unset)", colorterm ? colorterm : "(unset)");
  std::fprintf(stderr, "  NO_COLOR         : %s\n",
               std::getenv("NO_COLOR") ? "set (colours suppressed)" : "unset");
  std::fprintf(stderr, "  text font        : whatever the terminal is "
                       "configured for (this build assumes a mono font)\n");
  const std::pair<const char *, Icon> icons[] = {
      {"shuffle", Icon::Shuffle}, {"previous", Icon::Previous},
      {"play", Icon::Play},       {"pause", Icon::Pause},
      {"next", Icon::Next},       {"repeat", Icon::Repeat},
      {"speaker", Icon::Speaker},
  };
  for (const auto &entry : icons) {
    const std::string glyph = iconGlyph(entry.second, icon_set_);
    std::fprintf(stderr, "  icon %-8s    :", entry.first);
    for (const char raw : glyph) {
      const auto byte = static_cast<unsigned char>(raw);
      std::fprintf(stderr, " %02X", byte);
    }
    std::fprintf(stderr, "  (utf8)");
    for (std::size_t index = 0; index < glyph.size();) {
      const auto lead = static_cast<unsigned char>(glyph[index]);
      unsigned codepoint = lead;
      std::size_t length = 1;
      if ((lead & 0xE0U) == 0xC0U) {
        codepoint = lead & 0x1FU;
        length = 2;
      } else if ((lead & 0xF0U) == 0xE0U) {
        codepoint = lead & 0x0FU;
        length = 3;
      } else if ((lead & 0xF8U) == 0xF0U) {
        codepoint = lead & 0x07U;
        length = 4;
      }
      for (std::size_t k = 1; k < length && index + k < glyph.size(); ++k) {
        codepoint = (codepoint << 6U) |
                    (static_cast<unsigned char>(glyph[index + k]) & 0x3FU);
      }
      const bool pua = codepoint >= 0xE000U && codepoint <= 0xF8FFU;
      std::fprintf(stderr, " U+%04X%s", codepoint,
                   pua ? " [PUA: needs a Nerd Font]" : " [standard]");
      index += length;
    }
    std::fprintf(stderr, "\n");
  }
  std::fprintf(stderr, "  icon set         : %s\n", iconSetName(icon_set_));
  std::fprintf(stderr, "  slider renderer  : %s\n",
               sliderRendererName(slider_renderer_));
  std::fprintf(stderr, "  transport gap    : %d cells\n",
               controller_.config().transport_gap);
}

void Application::runScriptLine(const std::string &raw) {
  if (script_output_ == nullptr || root_ == nullptr)
    return;
  // std::cout's buffer is the report file while a script runs, so the report is
  // written where fd 1 cannot swallow it.
  std::ostream &out = std::cout;

  const std::size_t first = raw.find_first_not_of(" \t\r");
  if (first == std::string::npos || raw[first] == '#')
    return;
  const std::string line = raw.substr(first);
  std::istringstream words_stream(line);
  std::vector<std::string> words;
  std::string word;
  while (words_stream >> word)
    words.push_back(std::move(word));
  if (words.empty())
    return;
  const std::string &command = words[0];

  // The rest of the line after `count` words, verbatim, with one enclosing pair
  // of quotes removed: an expectation may contain spaces.
  const auto tail = [&](std::size_t count) {
    std::size_t position = 0;
    for (std::size_t index = 0; index < count; ++index) {
      position = line.find_first_not_of(" \t", position);
      if (position == std::string::npos)
        return std::string{};
      position = line.find_first_of(" \t", position);
      if (position == std::string::npos)
        return std::string{};
    }
    position = line.find_first_not_of(" \t", position);
    if (position == std::string::npos)
      return std::string{};
    std::string result = line.substr(position);
    if (result.size() >= 2U && result.front() == '"' && result.back() == '"')
      result = result.substr(1, result.size() - 2U);
    return result;
  };
  const auto fail = [&](const std::string &message) {
    ++script_failures_;
    out << "FAIL " << command << ": " << message << '\n';
  };

  if (command == "size") {
    const std::string spec = words.size() > 1 ? words[1] : std::string{};
    const auto cross = spec.find('x');
    try {
      resizeScript(std::stoi(spec.substr(0, cross)),
                   std::stoi(spec.substr(cross + 1)));
    } catch (const std::exception &) {
      fail("expected WIDTHxHEIGHT");
    }
  } else if (command == "sleep") {
    // Let real time pass, with the application's own workers running, so an
    // asynchronous backend (MPD advancing a track) is actually observed before
    // the next assertion. The first line of a script starts the workers.
    if (!backend_workers_started_) {
      // A scripted run exercises the REAL workers: the same MPD event loop,
      // the same timer, and the same analyzer a normal session starts in run().
      // Without the analyzer the visualizer would report an empty spectrum and
      // a scripted check could not tell "no signal" from "not running".
      configureVisualizer();
      startBackendEvents();
      startTicker();
      backend_workers_started_ = true;
    }
    const double seconds = words.size() > 1 ? std::stod(words[1]) : 0.0;
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(
                           static_cast<long long>(seconds * 1000.0));
    while (std::chrono::steady_clock::now() < until)
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } else if (command == "resize") {
    const std::string spec = words.size() > 1 ? words[1] : std::string{};
    const auto cross = spec.find('x');
    try {
      resizeScript(std::stoi(spec.substr(0, cross)),
                   std::stoi(spec.substr(cross + 1)));
    } catch (const std::exception &) {
      fail("expected WIDTHxHEIGHT");
    }
  } else if (command == "load") {
    controller_.initialize();
  last_reconnect_ = std::chrono::steady_clock::now();
    // One attempt at load, then the retry policy takes over: without this the
    // first tick would immediately try again.
    last_reconnect_ = std::chrono::steady_clock::now();
  } else if (command == "key") {
    if (words.size() < 2)
      fail("expected a key token");
    else
      (void)scriptKey(words[1]);
  } else if (command == "keys") {
    for (std::size_t index = 1; index < words.size(); ++index)
      (void)scriptKey(words[index]);
  } else if (command == "type") {
    // A key token first ("type g g" is a CHORD, "type PageDown" a key name),
    // and plain TEXT otherwise: "type 6621" must deliver four characters. The
    // fallback used to be missing, so a multi-character token was dropped
    // silently and "the field was typed into" became an assertion that could
    // never fail.
    const std::string text = tail(1);
    if (!scriptKey(text)) {
      for (const std::string &piece : scriptCodepoints(text))
        (void)scriptKey(piece);
    }
  } else if (command == "state") {
    // Render once before reporting: the summary describes the CURRENT frame,
    // and metrics are recomputed during a render. Without this, a `size`
    // followed immediately by `state` reported the previous geometry.
    (void)scriptFrame();
    out << "state " << scriptStateSummary() << '\n';
  } else if (command == "dump") {
    for (const std::string &row : scriptFrame())
      out << "| " << row << '\n';
  } else if (command == "expect" || command == "expect-absent") {
    const std::string needle = tail(1);
    const auto rows = scriptFrame();
    const bool found = std::any_of(
        rows.begin(), rows.end(), [&](const std::string &row) {
          return row.find(needle) != std::string::npos;
        });
    if (found != (command == "expect"))
      fail(std::string(command == "expect" ? "missing: " : "unexpected: ") +
           needle);
  } else if (command == "expect-state") {
    // `expect-state key=value ...`: every pair must match the state summary.
    // The playing marker and the playback context are STATE, not pixels, so
    // this is how a script asserts the rule itself rather than a screenshot of
    // it. A pair whose key is missing fails, which is what catches a summary
    // that silently stopped reporting a field.
    std::map<std::string, std::string> reported;
    {
      std::istringstream fields(scriptStateSummary());
      std::string field;
      while (fields >> field) {
        const std::size_t equals = field.find('=');
        if (equals == std::string::npos)
          continue;
        reported[field.substr(0, equals)] = field.substr(equals + 1);
      }
    }
    for (std::size_t index = 1; index < words.size(); ++index) {
      const std::size_t equals = words[index].find('=');
      if (equals == std::string::npos) {
        fail("expected key=value, got: " + words[index]);
        continue;
      }
      const std::string key = words[index].substr(0, equals);
      const std::string wanted = words[index].substr(equals + 1);
      const auto found = reported.find(key);
      if (found == reported.end())
        fail("state does not report " + key);
      else if (found->second != wanted)
        fail(key + " is " + found->second + ", expected " + wanted);
    }
  } else if (command == "expect-state-not") {
    // `expect-state-not key=value ...`: the reported value must DIFFER. This is
    // how "the key did something" is asserted without hardcoding how far a
    // half-page jump lands, which depends on the terminal height.
    std::map<std::string, std::string> reported;
    {
      std::istringstream fields(scriptStateSummary());
      std::string field;
      while (fields >> field) {
        const std::size_t equals = field.find('=');
        if (equals == std::string::npos)
          continue;
        reported[field.substr(0, equals)] = field.substr(equals + 1);
      }
    }
    for (std::size_t index = 1; index < words.size(); ++index) {
      const std::size_t equals = words[index].find('=');
      if (equals == std::string::npos) {
        fail("expected key=value, got: " + words[index]);
        continue;
      }
      const std::string key = words[index].substr(0, equals);
      const std::string unwanted = words[index].substr(equals + 1);
      const auto found = reported.find(key);
      if (found == reported.end())
        fail("state does not report " + key);
      else if (found->second == unwanted)
        fail(key + " is still " + unwanted + ", expected it to change");
    }
  } else if (command == "expect-state-same") {
    // `expect-state-same key ...`: each key's value must equal the FIRST value
    // this run reported for it. A "must not change" rule -- the playing
    // marker, a session's snapshot size -- is a property of the whole run, so
    // it is asserted against the run's own baseline.
    std::map<std::string, std::string> reported;
    {
      std::istringstream fields(scriptStateSummary());
      std::string field;
      while (fields >> field) {
        const std::size_t equals = field.find('=');
        if (equals == std::string::npos)
          continue;
        reported[field.substr(0, equals)] = field.substr(equals + 1);
      }
    }
    for (std::size_t index = 1; index < words.size(); ++index) {
      const auto found = reported.find(words[index]);
      if (found == reported.end()) {
        fail("state does not report " + words[index]);
        continue;
      }
      const auto baseline = script_baseline_.find(words[index]);
      if (baseline == script_baseline_.end()) {
        script_baseline_[words[index]] = found->second;
        continue;
      }
      if (baseline->second != found->second)
        fail(words[index] + " changed from " + baseline->second + " to " +
             found->second);
    }
  } else if (command == "expect-row") {
    // `expect-row N text`: frame row N -- 0 is the frame's top border -- must
    // contain `text`. A positional assertion catches what `expect` cannot: the
    // right words rendered in the wrong place.
    int row = -1;
    try {
      row = words.size() > 1 ? std::stoi(words[1]) : -1;
    } catch (const std::exception &) {
      row = -1;
    }
    const std::string needle = tail(2);
    const auto rows = scriptFrame();
    if (row < 0 || row >= static_cast<int>(rows.size()))
      fail("row " + std::to_string(row) + " is outside the frame");
    else if (rows[static_cast<std::size_t>(row)].find(needle) ==
             std::string::npos)
      fail("row " + std::to_string(row) + " does not contain: " + needle);
  } else if (command == "quit") {
    // The real quit path, minus the terminal teardown a headless run never
    // took over. Checked here: the request was recorded and audio was stopped.
    requestQuit();
    if (!quitting_.load())
      fail("quit was not recorded");
    if (!state_.demo && state_.player.state == PlaybackState::Playing)
      fail("playback was not stopped");
  } else {
    fail("unknown command");
  }

  // The pump loop runs a single turn per command, which is not enough for the
  // application's own tick to fire. Running it here exercises the REAL path --
  // the same processTimers() a live session calls -- so the script observes the
  // clock and the motion update exactly as they happen.
  processTimers();
}

int Application::runScript(std::istream &script, std::ostream &out) {
  scripting_check_ = true;
  script_baseline_.clear();
  // FTXUI emits terminal setup codes even for a fixed-size screen, on both
  // output descriptors, and they are not this mode's product -- so 1 and 2 are
  // parked on /dev/null for the run. The report therefore goes to a file of its
  // own (replayed to the caller afterwards), because writing it to fd 1 would
  // silence it too.
  const std::string report_path =
      (std::filesystem::temp_directory_path() / "termusic-ui-script.out")
          .string();
  std::ofstream report_file(report_path, std::ios::trunc);
  std::streambuf *saved_cout = std::cout.rdbuf(report_file.rdbuf());
  const int saved_out = ::dup(STDOUT_FILENO);
  const int saved_err = ::dup(STDERR_FILENO);
  const int sink = ::open("/dev/null", O_WRONLY);
  if (sink >= 0) {
    (void)::dup2(sink, STDOUT_FILENO);
    (void)::dup2(sink, STDERR_FILENO);
  }

  script_output_ = &out;
  std::string line;
  while (std::getline(script, line)) {
    runScriptLine(line);
    // One pump per command. FTXUI executes a posted callback inline when no
    // application is active, so a command that triggers a button handler must
    // be followed by a turn of a REAL loop -- otherwise that handler runs
    // re-entrantly, inside the dispatch that produced it. A loop at a fixed
    // size never touches the terminal, so this needs no TTY.
    pumpLoop();
  }
  if (saved_out >= 0) {
    (void)::dup2(saved_out, STDOUT_FILENO);
    ::close(saved_out);
  }
  if (saved_err >= 0) {
    (void)::dup2(saved_err, STDERR_FILENO);
    ::close(saved_err);
  }
  if (sink >= 0)
    ::close(sink);
  std::cout.rdbuf(saved_cout);
  report_file.close();
  // The report belongs to the caller's stream, wherever that goes.
  {
    std::ifstream replay(report_path);
    out << replay.rdbuf();
  }
  std::error_code ignored;
  std::filesystem::remove(report_path, ignored);
  scripting_check_ = false;
  script_output_ = nullptr;
  return script_failures_;
}

void Application::pumpLoop() {
  // A fixed-size screen is rendered entirely in memory, so this works without a
  // terminal while still being a REAL loop: posted callbacks are deferred
  // (never run inline) and the application's own frame is the one rendered, so
  // the timer tick -- which is what notices a track change -- runs exactly as
  // it does in a normal session.
  const auto [width, height] = liveSize();
  ScreenInteractive pump = ScreenInteractive::FixedSize(width, height);
  // FTXUI renders through std::cout, and while a script runs that stream is the
  // report file: without this, every pumped frame would be captured as a wall
  // of ANSI codes in the middle of the report. The buffer is parked on
  // /dev/null for the duration of the pump and put back right after, so the
  // script's own output stays readable.
  std::ofstream frame_sink("/dev/null");
  std::streambuf *saved_buffer = std::cout.rdbuf(frame_sink.rdbuf());
  // Queued BEFORE the loop opens, so it runs on the first iteration: one turn
  // is all a pump is for.
  pump.Post([&pump] { pump.Exit(); });
  pump.Loop(Renderer([this] { return renderRoot(); }));
  std::cout.rdbuf(saved_buffer);
}

int Application::run() {
  screen_.TrackMouse(true);
  if (ui_slider_test_)
    printDiagnostics();
  controller_.initialize();
  last_reconnect_ = std::chrono::steady_clock::now();
  startBackendEvents();
  configureVisualizer();
  analyzer_.setPlaybackActive(state_.player.state == PlaybackState::Playing);
  startTicker();
  screen_.Loop(root_);
  stopWorkers();
  backend_.disconnect();
  return 0;
}

namespace {

/// Wall-clock budget for the WHOLE shutdown, armed the moment quitting is
/// committed to. It must sit above the bounded worst case, or it would preempt
/// the graceful path instead of merely backstopping it: an MPD whose socket
/// goes silent costs two 2 s timeouts (the stop command and the final
/// connection close), measured at ~4.3 s, and every other step is bounded by
/// the 250 ms event poll or an instant worker wakeup. Eight seconds leaves
/// roughly 2x headroom over that, so a healthy or merely-unresponsive MPD
/// always finishes on its own and only a pathological hang reaches this.
constexpr std::chrono::seconds kQuitWatchdog{8};

/// Idle gap after which a recorded keybinding is committed. Long enough for
/// `g n` / `y y` / `Ctrl+w h`, short enough not to feel stuck.
constexpr int kCaptureTimeoutMs = 1000;

/// The Library section's slot in the top-level tab container.
///
/// Library renders through `renderRoot()` (workspace or now-playing) rather
/// than through its component, so this carries focus and event ownership for
/// the section. It must stay focusable and must never claim an event: a
/// non-focusable or greedy slot makes FTXUI hand startup focus to whichever
/// widget it finds next, which is how a retired page's search box once ended
/// up swallowing every keystroke in the application.
class LibrarySectionComponent final : public ComponentBase {
public:
  Element OnRender() override { return text(""); }
  bool Focusable() const override { return true; }
  bool OnEvent(Event) override { return false; }
};

/// Gives the application a pre-order look at every event.
///
/// FTXUI routes an event through the whole component tree before the
/// screen-level `CatchEvent`, and `Container::Tab` claims `Event::Tab` for its
/// own selector (container.cpp). A presentation-mode key therefore cannot be
/// handled in `CatchEvent`: this wrapper sees the event first.
class PreEventComponent final : public ComponentBase {
public:
  PreEventComponent(Component child, std::function<bool(Event)> handler)
      : child_(std::move(child)), handler_(std::move(handler)) {}

  Element OnRender() override { return child_->Render(); }

  bool OnEvent(Event event) override {
    return handler_(event) || child_->OnEvent(event);
  }

  bool Focusable() const override { return child_->Focusable(); }
  // Must be the direct child: `Focused()` walks this chain from the root, so
  // skipping a level would make every descendant report itself unfocused.
  Component ActiveChild() override { return child_; }

private:
  Component child_;
  std::function<bool(Event)> handler_;
};

} // namespace

void Application::buildComponents() {
  // `core` is built as a module tree: the panel assembles every entry, and
  // this function only installs the result in the tab order. Nothing here
  // knows what a section contains.
  core_panel_ = std::make_unique<core::CorePanel>();
  core_panel_->build(*core_context_);

  // Tab order is `allPages()` order: Library, Settings. The Library slot
  // carries focus only -- renderRoot() drives that section's content.
  library_section_component_ = Make<LibrarySectionComponent>();
  page_tab_ = Container::Tab(
      {library_section_component_, core_panel_->component()}, &page_index_);

  // Reference player controls: plain line icons, no filled button chrome.
  // The active play/pause control carries a thin magenta ring.
  shuffle_button_ = Button(
      &shuffle_label_, [this] { dispatch(Action::ToggleShuffle); },
      [this] {
        ButtonOption option = ButtonOption::Simple();
        option.transform = [this](const EntryState &state) {
          const bool on = state_.player.random;
          Element entry = text(" " + state.label + " ");
          if (state.focused)
            entry = entry | bgcolor(theme_.hover_bg);
          return entry | color(on ? theme_.accent_primary : theme_.text) |
                 (on ? bold : nothing);
        };
        return option;
      }());
  const auto stepButton = [this](std::string label, Action action) {
    ButtonOption option = ButtonOption::Simple();
    option.transform = [this](const EntryState &state) {
      Element entry = text(" " + state.label + " ");
      if (state.focused)
        entry = entry | bgcolor(theme_.hover_bg);
      return entry | color(theme_.text);
    };
    return Button(std::move(label), [this, action] { dispatch(action); }, option);
  };
  previous_button_ = stepButton("⏮", Action::Previous);
  play_button_ = Button(
      &play_label_, [this] { dispatch(Action::TogglePlay); },
      [this] {
        ButtonOption option = ButtonOption::Simple();
        option.transform = [this](const EntryState &state) {
          // Reference focus ring: a small rounded rectangle around the icon,
          // magenta while a track is playing. One cell cannot draw a 1 px
          // ring, so the box is three rows tall.
          const bool playing = state_.player.state == PlaybackState::Playing;
          const Color ring = playing ? theme_.accent_primary : theme_.border_dim;
          const std::string inner = " " + state.label + " ";
          const int box_width = util::displayWidth(inner) + 2;
          return vbox({
                     text("╭" + util::repeat(box_width - 2, "\u2500") + "╮") |
                         color(ring),
                     text("│" + inner + "│") |
                         color(playing ? theme_.accent_primary : theme_.text) |
                         bold,
                     text("╰" + util::repeat(box_width - 2, "\u2500") + "╯") |
                         color(ring),
                 }) |
                 vcenter;
        };
        return option;
      }());
  next_button_ = stepButton("⏭", Action::Next);
  SliderOption<double> progress_option;
  progress_option.value = &progress_slider_;
  progress_option.min = &progress_min_;
  progress_option.max = &progress_max_;
  progress_option.increment = &progress_increment_;
  progress_option.color_active = theme_.progress_filled;
  progress_option.color_inactive = theme_.progress_empty;
  progress_option.on_change = [this] {
    if (!updating_sliders_ && state_.player.duration_seconds > 0.0) {
      controller_.seekAbsolute(progress_slider_);
    }
  };
  progress_slider_component_ = Slider(progress_option);

  SliderOption<int> volume_option;
  volume_option.value = &volume_slider_;
  volume_option.min = &volume_min_;
  volume_option.max = &volume_max_;
  volume_option.increment = &volume_increment_;
  volume_option.color_active = theme_.progress_filled;
  volume_option.color_inactive = theme_.progress_empty;
  volume_option.on_change = [this] {
    if (!updating_sliders_)
      controller_.setVolume(volume_slider_);
  };
  volume_slider_component_ = Slider(volume_option);

  player_container_ = Container::Horizontal(
      {shuffle_button_, previous_button_, play_button_, next_button_});

  modal_input_ = Input(&modal_text_, "playlist name");
  // The search line: ONE persistent FTXUI Input. It owns the caret, the UTF-8
  // editing (backspace and Delete work on CODEPOINTS, never bytes) and, through
  // FTXUI's focus/cursor path, the terminal cursor -- which is where a terminal
  // IME anchors its preedit and its candidate window. It is created here, once,
  // and never rebuilt: a redraw only re-renders its element.
  {
    InputOption search_option;
    search_option.placeholder = "";
    search_option.multiline = false;
    search_option.on_change = [this] {
      // Live filtering: the query changed, so the view does.
      rebuildSearchRows();
      screen_.PostEvent(Event::Custom);
    };
    search_input_ = Input(&search_buffer_, search_option);
  }
  auto target_option = styledMenu(theme_);
  target_option.on_enter = [this] { confirmModal(); };
  playlist_target_menu_ =
      Menu(&playlist_target_rows_, &playlist_target_, target_option);

  main_container_ = Container::Vertical({page_tab_, player_container_});
  root_ = CatchEvent(Renderer(main_container_, [this] { return renderRoot(); }),
                     [this](Event event) { return handleEvent(event); });
  // Global keys are handled above page focus routing: `Container::Tab` claims
  // Event::Tab for its own selector, and the screen-level CatchEvent only runs
  // after the whole tree.
  root_ = Make<PreEventComponent>(
      root_, [this](Event event) { return handleGlobalKey(event); });
}

void Application::startBackendEvents() {
  backend_.startEventLoop([this](BackendEvent changed) {
    screen_.Post([this, changed] { controller_.handleBackendEvent(changed); });
  });
}

void Application::configureVisualizer() {
  analyzer_.stop();
  // The Spectrum is the display, so a reconfiguration always ends with the
  // analyzer running: there is no "off" mode to fall back to.
  if (visualizer_ != nullptr)
    visualizer_->reset();
  if (state_.demo) {
    // Reference mode: keep the fixed reference shape for reproducible frames.
    loadReferenceSpectrum(
        state_.visualizer,
        static_cast<std::size_t>(
            std::max(8, controller_.config().visualizer_bar_density)));
    return;
  }
  const Config &config = controller_.config();
  state_.visualizer.bars.assign(
      static_cast<std::size_t>(config.visualizer_bar_density), 0.0F);
  state_.visualizer.peaks.assign(
      static_cast<std::size_t>(config.visualizer_bar_density), 0.0F);
  state_.visualizer.data_available = false;
  analyzer_.start(
      config.visualizer_fifo, config.visualizer_sample_rate,
      config.visualizer_channels, config.visualizer_bar_density,
      config.visualizer_sensitivity, config.visualizer_refresh_hz,
      // The callback only asks for a repaint; the spectrum itself is pulled by
      // the UI (see syncVisualizerSpectrum), so `state_.visualizer` has exactly
      // one writer and does not depend on the event loop being pumped. While
      // the search line owns the keyboard the request is SKIPPED -- the
      // analysis continues, but the screen is a static list, and repainting it
      // once per analysed frame is what thrashes an IME's composition.
      [this] {
        if (search_typing_.load())
          return;
        screen_.Post([this] { screen_.PostEvent(Event::Custom); });
      });
  analyzer_.setPlaybackActive(state_.player.state == PlaybackState::Playing);
}

void Application::startTicker() {
  // The ticker only requests repaints; it never queries MPD. While a track is
  // playing it runs fast enough (~30 FPS) that the interpolated thumb glides
  // instead of stepping once per second. When nothing is moving it drops to
  // 4 FPS so an idle player stays cheap.
  // While the search line owns the keyboard the screen is a static list, and
  // repainting it 30 times a second is exactly what makes a terminal IME's
  // composition flicker. The fast rate is therefore suspended for as long as
  // the box is open; closing it brings the animation straight back.
  ticker_fast_.store(
      !search_prompt_ &&
      (state_.player.state == PlaybackState::Playing || state_.demo ||
       ui_slider_test_ || motion_test_ || state_.visualizer.data_available));
  ticker_thread_ = std::jthread([this](std::stop_token stop) {
    std::unique_lock lock(ticker_mutex_);
    while (!stop.stop_requested()) {
      // Live spectrum and explicit diagnostic animations use the fast rate;
      // an unavailable FIFO must not keep an idle UI spinning at 30 FPS.
      const auto interval = std::chrono::milliseconds(
          ticker_fast_.load() ? ticker_fast_interval_ms_.load() : 250);
      ticker_wakeup_.wait_for(lock, stop, interval, [] { return false; });
      if (stop.stop_requested())
        break;
      lock.unlock();
      screen_.PostEvent(Event::Custom);
      lock.lock();
    }
  });
}

void Application::requestWorkerStop() {
  if (ticker_thread_.joinable())
    ticker_thread_.request_stop();
  ticker_wakeup_.notify_all();
  analyzer_.requestStop();
  backend_.requestEventLoopStop();
}

void Application::stopWorkers() {
  // Request every worker first so their bounded poll waits overlap instead of
  // making shutdown pay each timeout one after another.
  requestWorkerStop();
  if (ticker_thread_.joinable())
    ticker_thread_.join();
  analyzer_.stop();
  backend_.stopEventLoop();
}

void Application::armQuitWatchdog() {
  // LAST RESORT, and nothing else. Every normal step of the shutdown is
  // already bounded on its own (MPD sockets time out, the worker loops poll),
  // so this only ever fires when something pathological blocks the process --
  // a hung server call, a join that never returns, a future regression. It
  // deliberately does not replace those bounds.
  //
  // A detached thread is the right primitive here: a normal shutdown returns
  // from main and the process teardown destroys it, so it can never delay a
  // healthy exit, and there is no join that could itself hang.
  std::thread([] {
    std::this_thread::sleep_for(kQuitWatchdog);
    // Forced termination. Reported as FAILURE so a stuck shutdown is
    // distinguishable from a clean exit in logs, scripts and tests. Nothing is
    // written to the TUI -- the terminal may already be restored, or not.
    std::fputs("termusic: shutdown watchdog expired\n", stderr);
    std::fflush(stderr);
    std::_Exit(EXIT_FAILURE);
  }).detach();
}

void Application::requestQuit() {
  // Idempotent: N quit requests produce exactly one shutdown sequence and
  // therefore exactly one watchdog.
  if (quitting_.exchange(true))
    return;

  // Arm the deadline FIRST. It must cover the whole shutdown, including the
  // MPD stop below -- arming it afterwards would leave that call unguarded.
  armQuitWatchdog();

  // Stop producing callbacks, then explicitly stop MPD audio. Disconnecting
  // the client alone would leave the MPD daemon playing after this process
  // exits. Loop() can then restore the terminal, after which run() joins the
  // already-stopping workers and returns normally from main.
  requestWorkerStop();
  controller_.stopPlaybackForExit();
  screen_.Exit();
}

Element Application::renderMain() {
  // Presentation is checked first and above the section switch: immersive
  // now-playing is not a section and does not belong to any one of them.
  if (state_.presentation == PresentationMode::ImmersiveNowPlaying)
    return renderImmersiveNowPlaying();

  // Both sections render through the SAME frame: one border, one vertical
  // divider, tree on the left and whatever the selected node holds on the
  // right. `core` is not a different page, it is a different directory.
  return renderWorkspace();
}

Element Application::renderRoot() {
  // `state_.page` is authoritative: page changes can arrive from the navigation
  // menu, from a global key through the Controller, or from the settings page.
  page_index_ = pageIndex(state_.page);

  // Recompute every size from the live terminal size. This runs on each frame,
  // so a resize, maximise or restore is picked up without a restart and without
  // a special-case "too small" screen.
  const auto [live_width, live_height] = liveSize();
  metrics_ = computeMetrics(live_width, live_height,
                            controller_.config().transport_gap,
                            bottomBoxVisible());

  const auto now = std::chrono::steady_clock::now();
  updating_sliders_ = true;
  progress_max_ = std::max(1.0, state_.player.duration_seconds);
  progress_slider_ =
      std::clamp(displayElapsed(state_, now), 0.0, progress_max_);
  volume_slider_ = std::max(0, state_.player.volume);
  // The transport icon follows the playback state only: a selected-but-not-
  // playing queue row never changes it.
  play_label_ = iconGlyph(
      state_.player.state == PlaybackState::Playing ? Icon::Pause : Icon::Play,
      icon_set_);
  updating_sliders_ = false;

  const bool immersive =
      state_.presentation == PresentationMode::ImmersiveNowPlaying;

  Elements root_rows;
  if (immersive) {
    // The song owns a bright frame of its own; the control row sits inside it,
    // lifted one row off the bottom edge so it does not look glued on.
    Elements body;
    body.push_back(renderMain() | flex);
    // Logical separation from the controls: blank rows from the layout, so the
    // body's height and the gap can never disagree. No rule is drawn.
    body.push_back(blankRows(metrics_.immersive.player_gap_rows));
    body.push_back(renderPlayerBar());
    // `flex` is what makes the frame track the window: without it the frame
    // hugged its content and left the bottom of the terminal empty.
    root_rows.push_back(vbox(std::move(body)) |
                        borderStyled(ROUNDED, theme_.frame) | flex);
  } else {
    // The workspace is a single framed area. The player is NOT repeated here:
    // browsing and building is the job, and the transport lives in immersive.
    root_rows.push_back(renderMain() | flex);
    // The bottom row is EMPTY unless it is carrying an interaction: the input
    // box (search, prompts) or a destructive confirmation. There is no
    // permanent hint, no key legend and no status line.
    if (bottomBoxVisible())
      root_rows.push_back(renderBottomBox());
  }

  Element base =
      vbox(std::move(root_rows)) | bgcolor(theme_.background) | color(theme_.text);
  if (toastVisible())
    base = dbox({std::move(base), renderToast()});
  if (modal_ != Modal::None)
    base = dbox({std::move(base), renderModal() | center | clear_under});
  if (help_visible_)
    base = dbox({std::move(base), renderHelpOverlay() | center | clear_under});
  return base;
}


namespace {

/// Deterministic signal used only by the explicit
/// --ui-visualizer-motion-test diagnostic mode. Normal playback never
/// substitutes generated activity for missing PCM.
std::vector<float> fallbackSpectrum(int bands, double seconds) {
  std::vector<float> out(static_cast<std::size_t>(std::max(0, bands)), 0.0F);
  const int count = static_cast<int>(out.size());
  // A handful of narrow clusters at fixed positions, each with its own slow
  // breathing. Deterministic (no rand), so the shape is stable frame to frame
  // but still has the local peaks the reference shows.
  struct Cluster {
    double centre;
    double width;
    double gain;
    double rate;
  };
  static constexpr Cluster kClusters[] = {
      {0.03, 0.022, 0.82, 1.10}, {0.10, 0.016, 0.60, 1.70},
      {0.17, 0.019, 0.74, 0.90}, {0.24, 0.013, 0.46, 2.10},
      {0.30, 0.021, 0.66, 1.30}, {0.37, 0.014, 0.40, 1.90},
      {0.44, 0.018, 0.56, 1.05}, {0.52, 0.013, 0.33, 2.30},
      {0.59, 0.017, 0.47, 1.45}, {0.67, 0.014, 0.29, 1.80},
      {0.74, 0.018, 0.38, 1.15}, {0.82, 0.013, 0.22, 2.00},
      {0.89, 0.017, 0.26, 1.55}, {0.96, 0.014, 0.14, 1.25},
  };
  for (int index = 0; index < count; ++index) {
    const double x =
        count > 1 ? static_cast<double>(index) / static_cast<double>(count - 1)
                  : 0.0;
    // Overall downward tilt, as in the reference.
    // A slowly varying per-band bed instead of one constant floor, so quiet
    // regions breathe rather than forming a flat wall.
    double value = 0.030 + 0.022 * std::sin(x * 23.0 + seconds * 0.7) +
                   0.014 * std::sin(x * 57.0 - seconds * 1.1);
    for (const Cluster &cluster : kClusters) {
      const double d = (x - cluster.centre) / cluster.width;
      const double envelope = std::exp(-0.5 * d * d);
      const double breathe =
          0.75 + 0.25 * std::sin(seconds * cluster.rate + cluster.centre * 31.0);
      value += cluster.gain * envelope * breathe;
    }
    out[static_cast<std::size_t>(index)] =
        static_cast<float>(std::clamp(value, 0.02, 1.0));
  }
  return out;
}

} // namespace

void Application::syncTreeFromPlaylists() {
  // The controller keeps virtual Default at index 0, followed by MPD's saved
  // playlists. Every entry gets a tree row.
  std::string signature;
  std::vector<std::pair<std::string, std::string>> entries;
  for (std::size_t index = 0; index < state_.library.playlists.size(); ++index) {
    const std::string &name = state_.library.playlists[index];
    entries.emplace_back(name, name);
    signature += name;
    signature.push_back('\n');
  }
  if (signature == tree_synced_signature_)
    return; // j/k must never rebuild the tree
  tree_synced_signature_ = signature;
  workspace_tree_.setPlaylists(std::move(entries));
}

const termusic::TreeNode *Application::currentTreeNode() const {
  return workspace_tree_.current();
}

void Application::afterTreeFold() {
  // A fold changes how many rows the tree has, so the viewport is re-clamped
  // against the new list. The cursor was restored by the tree itself (by stable
  // node id), so it is still on the folder that was just folded.
  const int rows = static_cast<int>(workspace_tree_.visible().size());
  const int page_rows = std::max(1, metrics_.main_height - 5);
  tree_scroll_ = std::clamp(tree_scroll_, 0, std::max(0, rows - page_rows));
  // Folding is visibility only: the open collection, the settings section and
  // every draft value stay exactly as they were.
}

void Application::activateTreeNode() {
  TreeNode *node = const_cast<TreeNode *>(workspace_tree_.current());
  if (node == nullptr)
    return;
  if (node->type == TreeNodeType::Group ||
      node->type == TreeNodeType::VaultRoot ||
      node->type == TreeNodeType::CoreRoot) {
    workspace_tree_.toggleCurrentGroup();
    return;
  }
  loadTreeNode();
  // Enter means "open this and work in it": the pane it just filled takes the
  // keyboard, and the tree stays one `h` away. Merely MOVING the cursor does
  // not do this -- see autoLoadTreeCursor().
  workspace_pane_ = WorkspacePane::TrackList;
  focusCurrentList();
}

/// Fills the right pane from the Tree cursor without touching the keyboard.
void Application::loadTreeNode() {
  const TreeNode *node = workspace_tree_.current();
  if (node == nullptr)
    return;
  if (node->type != TreeNodeType::CoreSection &&
      state_.page != Page::Library) {
    // A `vault` collection is shown in the media workspace, so the page has to
    // follow: without this, opening one while `core` was displayed left the
    // config editor on screen and the tree and pane disagreed.
    selectPage(pageIndex(Page::Library));
  }
  if (node->type == TreeNodeType::CoreSection) {
    // `core` entries are files: the pane shows the editor behind the selected
    // one, in the same frame the media tables use.
    for (const ConfigFileEntry &file : kCoreSections) {
      if (file.id != node->id)
        continue;
      core_panel_->select(file.section);
      selectPage(pageIndex(Page::Settings));
      visual_message_ = "Editing " + std::string(file.label);
      return;
    }
    return;
  }
  // Opening a collection loads the Track Buffer. It never starts audio.
  if (node->type == TreeNodeType::History) {
    // Generated by playback, never edited: the rows come from the
    // application-owned history, not from MPD.
    active_collection_ = ActiveCollection::History;
    active_playlist_name_.clear();
    active_playlist_index_ = -1;
    refreshHistoryView();
    track_cursor_ = 0;
    track_scroll_ = 0;
    return;
  }
  if (node->type == TreeNodeType::Database) {
    // The real MPD media database, exposed as a read-only collection. Nothing
    // is copied and no saved playlist is created for it.
    active_collection_ = ActiveCollection::Library;
    active_playlist_name_.clear();
    active_playlist_index_ = -1;
    // Must go through selectDatabase(): it resets library.current, which is
    // what decides whether the database or a playlist is fetched.
    controller_.selectDatabase();
    track_cursor_ = 0;
    track_scroll_ = 0;
    return;
  }
  if (node->type == TreeNodeType::Playlist) {
    for (std::size_t index = 0; index < state_.library.playlists.size();
         ++index) {
      if (state_.library.playlists[index] == node->label) {
        active_collection_ = ActiveCollection::Playlist;
        active_playlist_name_ = node->id;
        active_playlist_index_ = static_cast<int>(index);
        controller_.selectPlaylist(static_cast<int>(index));
        track_cursor_ = 0;
        track_scroll_ = 0;
        return;
      }
    }
  }
}

void Application::refreshHistoryView() {
  history_songs_ = controller_.history().songsNewestFirst();
  history_revision_seen_ = controller_.history().revision();
}

void Application::configureKeymap() {
  keymap_.loadDefaults();
  // Obsolete entries are collected rather than reported one by one: an old
  // configuration file can list several removed shortcuts, and a wall of
  // warnings for keys that are simply gone would be noise.
  std::vector<std::string> obsolete;
  // The removals are applied AFTER the walk: `configured` IS the live
  // configuration map, so erasing from it inside the loop would invalidate the
  // iterators (it segfaulted the moment a file actually contained an obsolete
  // entry).
  std::vector<std::pair<KeyContext, Action>> obsolete_entries;
  const auto &configured = controller_.config().keybindings;
  for (const auto &[context_name, actions] : configured) {
    const auto context = contextFromName(context_name);
    if (!context) {
      keymap_warnings_.push_back("Unknown keybinding context: " + context_name);
      continue;
    }
    for (const auto &[action_name, sequences] : actions) {
      const std::optional<Action> resolved = actionFromId(action_name);
      if (resolved && !actionConfigurableIn(*resolved, *context)) {
        // The shortcut was withdrawn in a later release. Ignore it, and forget
        // it in memory so the next save writes it out -- without touching any
        // other key, and without rewriting the file at startup.
        obsolete.push_back(context_name + "." + action_name);
        obsolete_entries.emplace_back(*context, *resolved);
        continue;
      }
      std::string problem;
      if (!keymap_.applyOverride(*context, action_name, sequences, &problem)) {
        // Rejected overrides keep their built-in default; the rest of the
        // configuration still applies.
        keymap_warnings_.push_back("Keybinding rejected: " + context_name +
                                   "." + action_name + ": " + problem);
      }
    }
  }
  for (const std::string &problem : keymap_.validate())
    keymap_warnings_.push_back(problem);
  // Only now that the walk is finished: this mutates the map it walked.
  for (const auto &[context, action] : obsolete_entries)
    controller_.forgetKeyOverride(context, action);
  if (!obsolete.empty()) {
    std::string message = "Ignored " + std::to_string(obsolete.size()) +
                          " obsolete keybinding(s): " + obsolete.front();
    if (obsolete.size() > 1U)
      message += " (+" + std::to_string(obsolete.size() - 1U) + " more)";
    // First, so the one line the status bar shows is the actionable one.
    keymap_warnings_.insert(keymap_warnings_.begin(), std::move(message));
  }
  if (!keymap_warnings_.empty()) {
    // The workspace status line is the surface that actually renders; the
    // toast is kept in step for whatever renders it later.
    visual_message_ = keymap_warnings_.front();
    state_.toast = keymap_warnings_.front();
    state_.toast_at = std::chrono::steady_clock::now();
  }
}

std::vector<KeyContext> Application::currentKeyContexts() const {
  if (state_.presentation == PresentationMode::ImmersiveNowPlaying)
    return {KeyContext::Immersive, KeyContext::Global};
  switch (state_.page) {
  case Page::Library:
    // Visual deliberately does NOT inherit the Library or Tracks contexts:
    // `a`, `p` and Enter belong to normal panes and must not fire while a
    // selection is active.
    if (vim_mode_ == VimMode::Visual)
      return {KeyContext::Visual, KeyContext::Global};
    if (workspace_pane_ == WorkspacePane::Tree)
      return {KeyContext::Tree, KeyContext::Library, KeyContext::Global};
    return {KeyContext::Tracks, KeyContext::Library, KeyContext::Global};
  case Page::Settings:
    return {KeyContext::Settings, KeyContext::Global};
  }
  return {KeyContext::Global};
}

std::string Application::keyHint(Action action) const {
  return keyHint(currentKeyContexts(), action);
}

std::string Application::keyHint(const std::vector<KeyContext> &chain,
                                 Action action) const {
  const std::string binding = keymap_.primaryBinding(chain, action);
  if (binding.empty())
    return std::string();
  // Display form only: a repeated token collapses the way Vim writes it, so
  // the "y y" sequence is advertised as "yy".
  std::vector<std::string> tokens;
  std::string token;
  for (const char c : binding) {
    if (c == ' ') {
      if (!token.empty())
        tokens.push_back(token);
      token.clear();
      continue;
    }
    token += c;
  }
  if (!token.empty())
    tokens.push_back(token);
  if (tokens.size() > 1) {
    const bool repeated = std::all_of(
        tokens.begin(), tokens.end(),
        [&](const std::string &t) { return t == tokens.front(); });
    if (repeated) {
      std::string joined;
      for (const std::string &t : tokens)
        joined += t;
      tokens.assign(1, joined);
    }
  }
  std::string rendered;
  for (const std::string &t : tokens) {
    if (!rendered.empty())
      rendered += ' ';
    rendered += (t == "Up")      ? "\u2191"
                : (t == "Down")  ? "\u2193"
                : (t == "Left")  ? "\u2190"
                : (t == "Right") ? "\u2192"
                                 : t;
  }
  return rendered;
}

bool Application::performKeymapAction(Action action) {
  // Each action starts from a clean tone; only a refusal sets it again.
  visual_message_error_ = false;
  const bool on_tree = workspace_pane_ == WorkspacePane::Tree;
  const int page_rows = std::max(1, metrics_.main_height - 5);
  const int track_count = static_cast<int>(activeTracks().size());

  switch (action) {
  case Action::SectionPrevious:
  case Action::SectionNext: {
    const int index = pageIndex(state_.page);
    const int next = std::clamp(
        index + (action == Action::SectionNext ? 1 : -1), 0, kPageCount - 1);
    if (next != index)
      selectPage(next);
    return true;
  }
  case Action::OpenImmersive:
    state_.presentation = PresentationMode::ImmersiveNowPlaying;
    return true;
  case Action::ToggleImmersive:
    // One action toggles: the section underneath is never changed.
    state_.presentation =
        state_.presentation == PresentationMode::ImmersiveNowPlaying
            ? PresentationMode::Normal
            : PresentationMode::ImmersiveNowPlaying;
    return true;
  case Action::FocusTree:
    workspace_pane_ = WorkspacePane::Tree;
    focusCurrentList();
    return true;
  case Action::FocusTracks:
    workspace_pane_ = WorkspacePane::TrackList;
    focusCurrentList();
    return true;
  case Action::MoveDown:
  case Action::MoveUp:
    return moveWorkspaceCursor(action == Action::MoveDown ? 1 : -1);
  case Action::MoveToFirst:
    if (on_tree) {
      workspace_tree_.toFirst();
      tree_scroll_ = 0;
      // Every way of landing on a node loads it, not just j/k: a jump that
      // left the previous collection in the buffer let the Track cursor and
      // the Tree cursor disagree about which collection is open.
      autoLoadTreeCursor();
    } else {
      track_cursor_ = 0;
      track_scroll_ = 0;
    }
    return true;
  case Action::MoveToLast:
    if (on_tree) {
      workspace_tree_.toLast();
      tree_scroll_ =
          std::max(0, static_cast<int>(workspace_tree_.visible().size()) - page_rows);
      autoLoadTreeCursor();
    } else {
      track_cursor_ = std::numeric_limits<int>::max();
    }
    return true;
  case Action::HalfPageDown:
  case Action::HalfPageUp: {
    const int direction = action == Action::HalfPageDown ? 1 : -1;
    if (on_tree) {
      workspace_tree_.halfPage(direction, page_rows);
      autoLoadTreeCursor();
    } else {
      track_cursor_ += direction * std::max(1, page_rows / 2);
    }
    return true;
  }
  case Action::MoveLeft:
    // With the TREE focused, `h` on an expanded folder collapses it -- the
    // standard tree gesture, and the same operation Enter performs. Anywhere
    // else `h` returns to the tree: local horizontal movement IS pane movement,
    // in a settings module too. Changing a value is Enter's job (flip / cycle /
    // edit / run), so h/l keep exactly one meaning.
    if (on_tree && workspace_tree_.collapseFocusedFolder()) {
      afterTreeFold();
      return true;
    }
    workspace_pane_ = WorkspacePane::Tree;
    focusCurrentList();
    return true;
  case Action::MoveRight:
    // `l` on a COLLAPSED folder expands it, symmetrically. On a leaf (or on an
    // already expanded folder) it keeps its existing meaning: enter the content
    // pane, where the module's own cursor lives.
    if (on_tree && workspace_tree_.expand()) {
      afterTreeFold();
      return true;
    }
    workspace_pane_ = WorkspacePane::TrackList;
    focusCurrentList();
    return true;
  case Action::Activate:
    // Enter belongs to whatever owns the keyboard. On the settings page that is
    // the PANE -- but only when the pane really has it: with the tree focused,
    // Enter is the tree's own key (open a collection, toggle a folder), exactly
    // as it is on the vault page.
    if (state_.page == Page::Settings && workspace_pane_ == WorkspacePane::TrackList) {
      // A settings module owns Enter whenever its pane holds the keyboard:
      // the shared list engine turns it into "flip / edit / run / cycle".
      if (workspace_pane_ == WorkspacePane::TrackList && !keybindingsOpen()) {
        core::SettingsSection *section = selectedCoreSection();
        return section != nullptr && section->activate();
      }
      // The binding table is the one module with an interface of its own, so
      // Enter only reaches it through the explicit branch below.
      if (!keybindingsOpen())
        return false;
      // Enter on the table has two meanings and the table decides which: the
      // fixed reset control (recovery, never a configurable shortcut) or the
      // selected row (start recording a new binding).
      core::KeybindingsSection *table = keybindings();
      if (table == nullptr)
        return false;
      if (table->resetPending() || table->capturing())
        return true;
      if (table->armResetSelected()) {
        visual_message_.clear();
        return true;
      }
      if (table->beginCapture())
        visual_message_.clear();
      return true;
    }
    if (on_tree) {
      activateTreeNode();
      return true;
    }
    if (track_count > 0) {
      const int index = std::clamp(track_cursor_, 0, track_count - 1);
      // THE operation that creates or replaces the playback context: the
      // collection being browsed is snapshotted and becomes the owner of
      // playback. Merely BROWSE a list and it stays unmarked.
      controller_.playTrackAt(activeTracks(), index,
                              browsedPlaybackCollection());
    }
    return true;
  case Action::CreatePlaylist:
    playlist_prompt_ = true;
    playlist_prompt_text_.clear();
    visual_message_.clear();
    return true;
  case Action::PasteRegister:
    if (on_tree)
      pasteRegisterToTreeSelection();
    return true;
  case Action::RenamePlaylist: {
    const int index = treePlaylistIndex();
    if (index <= 0) {
      visual_message_ = index == 0 ? "Default cannot be renamed"
                                   : "Not a saved playlist";
      visual_message_error_ = true;
      return true;
    }
    playlist_prompt_ = true;
    playlist_prompt_rename_ = true;
    rename_original_ = state_.library.playlists[static_cast<std::size_t>(index)];
    playlist_prompt_text_ = rename_original_;
    visual_message_.clear();
    return true;
  }
  case Action::DeletePlaylist: {
    // `dd` only ever deletes a saved playlist; a collection entry is removed
    // with DeleteCurrent from the Track list.
    const int index = treePlaylistIndex();
    if (index <= 0) {
      visual_message_ = index == 0 ? "Default cannot be deleted"
                                   : "Not a saved playlist";
      visual_message_error_ = true;
      return true;
    }
    delete_playlist_pending_ = true;
    visual_message_.clear();
    return true;
  }
  case Action::EnterVisual:
    if (!on_tree)
      enterVisualSelection();
    return true;
  case Action::YankCurrent:
    if (!on_tree)
      yankCurrentTrack();
    return true;
  case Action::YankSelection:
    if (visualActive())
      yankVisualSelection();
    return true;
  case Action::DeleteCurrent:
    if (!on_tree)
      deleteCurrentEntry();
    return true;
  case Action::DeleteSelection:
    if (visualActive())
      deleteVisualSelection();
    return true;
  case Action::MoveQueueItemUp:
  case Action::MoveQueueItemDown: {
    // Reordering edits playlist content, so it follows the same rule as every
    // other entry edit: only a user-owned playlist accepts it.
    if (!collectionWritable()) {
      visual_message_ = readOnlyReason();
      visual_message_error_ = true;
      return true;
    }
    const int delta = action == Action::MoveQueueItemUp ? -1 : 1;
    if (controller_.moveCurrentPlaylistItem(track_cursor_, delta)) {
      track_cursor_ = state_.library.selected;
      visual_message_ = "Reordered";
    }
    return true;
  }
  case Action::CancelVisual:
    if (state_.presentation == PresentationMode::ImmersiveNowPlaying) {
      state_.presentation = PresentationMode::Normal;
      return true;
    }
    if (visualActive()) {
      cancelVisualSelection();
      visual_message_.clear();
    }
    return true;
  case Action::Search:
  case Action::OpenSearch:
    // One action, two panes: the box searches whatever the focused pane shows.
    openSearchPrompt();
    return true;
  case Action::NextMatch:
  case Action::PreviousMatch: {
    if (search_pattern_.empty())
      return false; // fall through to the legacy keymap
    const int match =
        findMatch(track_cursor_, action == Action::NextMatch ? 1 : -1);
    if (match >= 0) {
      track_cursor_ = match;
      workspace_pane_ = WorkspacePane::TrackList;
      visual_message_.clear();
    } else {
      visual_message_ = "Pattern not found: " + search_pattern_;
    }
    return true;
  }
  // Actions migrated out of the retired Config::keymap. Their semantics are
  // unchanged; only the binding source moved.
  case Action::ResetBinding:
    if (core::KeybindingsSection *table = keybindings()) {
      table->resetSelected();
      syncKeymapMessage();
    }
    return true;
  case Action::ResetAllBindings:
    if (core::KeybindingsSection *table = keybindings())
      table->requestReset();
    visual_message_.clear();
    return true;
  case Action::Cancel:
    // In a settings module Esc closes what is open -- an edited field, or a
    // Select's curtain -- and only the NEXT Esc hands the keyboard back to the
    // tree. Closing is not a value change: whatever was active stays active.
    if (state_.page == Page::Settings &&
        workspace_pane_ == WorkspacePane::TrackList && !keybindingsOpen() &&
        !coreEditing()) {
      core::SettingsSection *section = selectedCoreSection();
      if (section != nullptr && section->leaveEditing()) {
        screen_.PostEvent(Event::Custom);
        return true;
      }
      workspace_pane_ = WorkspacePane::Tree;
      focusCurrentList();
      return true;
    }
    dispatch(action);
    return true;
  case Action::Quit:
  case Action::TogglePlay:
  case Action::VolumeUp:
  case Action::VolumeDown:
  case Action::ToggleRepeat:
  case Action::ToggleShuffle:
  case Action::Reconnect:
  case Action::UpdateDatabase:
    // Everything here is the controller's business; the UI only routes it.
    dispatch(action);
    return true;
  case Action::Previous:
  case Action::Next:
    // Transport follows the PLAYBACK CONTEXT -- the snapshot the run started
    // from -- never the browsed collection and never whatever MPD's queue
    // happened to hold. Automatic progression follows the same snapshot
    // because that snapshot IS the queue.
    dispatch(action);
    return true;
  case Action::PageLibrary:
  case Action::PageSettings: {
    // Vault / Core are tree roots now, so the page shortcuts move the tree
    // cursor to the root they name -- the same navigation, one level up.
    const char *target = action == Action::PageLibrary ? "vault" : "core";
    for (std::size_t index = 0; index < workspace_tree_.visible().size();
         ++index) {
      if (workspace_tree_.visible()[index].id != target)
        continue;
      workspace_pane_ = WorkspacePane::Tree;
      workspace_tree_.setCursor(static_cast<int>(index));
      workspace_tree_.expandCurrentRoot();
      autoLoadTreeCursor();
      return true;
    }
    return true;
  }
  case Action::GoParent:
  case Action::EnterDirectory:
  case Action::SeekBackward:
  case Action::SeekForward:
  case Action::Help:
    dispatch(action);
    return true;
  default:
    return false;
  }
}

bool Application::isQuitKey(const Event &event) const {
  if (event.is_mouse())
    return false;
  const std::string token = normalizeKey(event, false);
  if (token.empty())
    return false;
  // `q` is RESERVED. It is the one guaranteed way out: whatever the
  // configuration says, whatever pane holds the keyboard and whatever a stale
  // capture may have written, pressing `q` stops the music and leaves the
  // program. Without this a bad `[keybindings]` entry -- or a capture the user
  // never meant to start -- could leave no way out but killing the process.
  //
  // The key is named once, in the keymap header: the same constant is what
  // `Keymap::applyOverride` refuses to hand to any other action, so the
  // invariant and its validation can never drift apart.
  if (token == kReservedQuitKey)
    return true;
  // Quit is no longer a configurable entry, so the shipped `q` binding below is
  // the only other way it can be spelled. Reading it from the keymap (rather
  // than hardcoding a second time) keeps the reserved key and the semantic
  // action in step, and only an exact single-token binding counts: a chord
  // ending in the same key still goes through the prefix machinery.
  for (const KeyContext context : currentKeyContexts()) {
    for (const std::string &sequence : keymap_.bindingsFor(context, Action::Quit)) {
      if (sequence == token)
        return true;
    }
  }
  return false;
}

bool Application::handleGlobalKey(Event event) {
  // Custom events drive timer and repaint processing, so they must keep
  // flowing through normal dispatch even while a prompt waits for input.
  // Swallowing one here stops processTimers() and pending confirmations never
  // repaint.
  if (event == Event::Custom)
    return false;
  if (event.is_mouse()) {
    // The fixed recovery footer is hit-tested here rather than in the normal
    // mouse path: FTXUI's Container::Tab claims clicks inside its own region
    // first, so a post-order check never sees them. Recovery must not depend
    // on the component tree's cooperation.
    if (modal_ == Modal::None && state_.page == Page::Settings) {
      // The fixed recovery footer is hit-tested here rather than in the normal
      // mouse path: FTXUI's Container::Tab claims clicks inside its own region
      // first, so a post-order check never sees them. Recovery must not depend
      // on the component tree's cooperation.
      if (core::KeybindingsSection *table = keybindings();
          table != nullptr && table->hitsResetFooter(event.mouse())) {
        if (event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          table->focusFooter();
          table->requestReset();
          visual_message_.clear();
          screen_.PostEvent(Event::Custom);
        }
        return true;
      }
    }
    return false;
  }
  // A keyboard action between two clicks breaks the double-click gesture.
  last_click_track_ = -1;
  if (modal_ != Modal::None || capturingBinding())
    return false;

  // The settings pane is full of widgets that treat a printable key as input
  // (FTXUI's Slider moves on any of them). The tree is the navigation, so j/k
  // are taken here -- ahead of the widget -- exactly as they are in the media
  // panes. Only a text field and the binding table keep them.
  // A `core` pane is full of FTXUI widgets, and FTXUI's Slider treats ANY
  // printable key as "move by one" -- so a `j` meant for the tree would quietly
  // change the visualizer sensitivity instead. The workspace's row keys are
  // therefore taken here, ahead of the widget, exactly as they are in the media
  // panes. Only a text field (which the user is typing into) and the binding
  // table (which owns a row list of its own) keep them.
  if (state_.page == Page::Settings &&
      state_.presentation != PresentationMode::ImmersiveNowPlaying &&
      workspace_pane_ == WorkspacePane::TrackList &&
      !keybindingsOpen()) {
    const std::string token = normalizeKey(event, false);
    if (coreEditing())
      return false;
    if (!token.empty()) {
      for (const KeyContext context : currentKeyContexts()) {
        for (const std::string &sequence :
             keymap_.bindingsFor(context, Action::MoveDown)) {
          if (sequence == token)
            return moveWorkspaceCursor(1);
        }
        for (const std::string &sequence :
             keymap_.bindingsFor(context, Action::MoveUp)) {
          if (sequence == token)
            return moveWorkspaceCursor(-1);
        }
      }
    }
  }

  // Deleting a saved playlist is destructive: confirm before touching MPD.
  if (delete_playlist_pending_) {
    if (event == Event::Return) {
      delete_playlist_pending_ = false;
      const int index = treePlaylistIndex();
      const bool was_active =
          index >= 0 && active_playlist_name_ ==
                            state_.library.playlists[static_cast<std::size_t>(index)];
      if (index >= 0) {
        controller_.selectPlaylist(index);
        if (controller_.deleteCurrentPlaylist()) {
          visual_message_ = "Playlist deleted";
          if (was_active) {
            // Never leave a dangling collection: fall back to the media
            // database, the one collection that always exists.
            active_collection_ = ActiveCollection::Library;
            active_playlist_name_.clear();
            active_playlist_index_ = -1;
            controller_.selectDatabase();
            for (std::size_t at = 0; at < workspace_tree_.visible().size();
                 ++at) {
              if (workspace_tree_.visible()[at].type == TreeNodeType::Database) {
                workspace_tree_.setCursor(static_cast<int>(at));
                break;
              }
            }
            track_cursor_ = 0;
            track_scroll_ = 0;
          }
        } else {
          visual_message_ = "Failed to delete playlist";
        }
      }
    } else if (event == Event::Escape) {
      delete_playlist_pending_ = false;
      visual_message_ = "Delete cancelled";
    }
    screen_.PostEvent(Event::Custom);
    return true;
  }

  // Reset-all waits for an explicit confirmation before destroying the user's
  // bindings; one keystroke must never do it.
  if (core::KeybindingsSection *table = keybindings();
      table != nullptr && table->resetPending()) {
    if (event == Event::Return) {
      table->cancelReset();
      controller_.resetKeymap();
      keymap_.loadDefaults();
      visual_message_ = "All keybindings restored to defaults";
    } else if (event == Event::Escape) {
      table->cancelReset();
      visual_message_ = "Reset cancelled";
    }
    screen_.PostEvent(Event::Custom);
    return true;
  }

  // Text entry is reserved: it outranks the keymap so 0x7f edits text and
  // H/L/n/N/a/v/y/p are literal characters while a prompt is open.
  if (playlist_prompt_)
    return handlePlaylistPromptKey(event);
  if (search_prompt_)
    return handleSearchPromptKey(event);

  // The configurable layer. Features below never see a physical key.
  const std::string token = normalizeKey(event, false);
  if (token.empty())
    return false;
  const Keymap::Resolution resolution =
      keymap_.feed(currentKeyContexts(), token);

  if (resolution.result == Keymap::Result::Pending) {
    screen_.PostEvent(Event::Custom);
    return true;
  }
  if (resolution.result == Keymap::Result::Matched) {
    // Retire any previous transient feedback before the action sets its own.
    visual_message_.clear();
    const bool handled = performKeymapAction(resolution.action);
    screen_.PostEvent(Event::Custom);
    return handled;
  }
  return false; // unmapped: fall through to the legacy action keymap
}

bool Application::visualActive() const {
  return vim_mode_ == VimMode::Visual && visual_anchor_ >= 0;
}

const std::vector<Song> &Application::activeTracks() const {
  // History keeps its own buffer so a database refresh can never clobber it;
  // every other collection is served by library.songs.
  return active_collection_ == ActiveCollection::History ? history_songs_
                                                         : state_.library.songs;
}

std::string Application::collectionLabel() const {
  switch (active_collection_) {
  case ActiveCollection::Library:
    return "Library";
  case ActiveCollection::History:
    return "History";
  case ActiveCollection::Playlist:
    break;
  }
  return active_playlist_name_;
}

bool Application::overlayOwnsKeyboard() const {
  // A prompt, a destructive confirmation or the reset confirmation is a control
  // in its own right: while one is up it owns the keyboard, so no pane may look
  // focused behind it. A key CAPTURE is not an overlay -- the table is the
  // control taking the keys, and it shows the recording state itself.
  const core::KeybindingsSection *table = keybindings();
  return search_prompt_ || playlist_prompt_ || delete_playlist_pending_ ||
         (table != nullptr && table->resetPending());
}

bool Application::keybindingsOpen() const {
  if (core_panel_ == nullptr || state_.page != Page::Settings)
    return false;
  const TreeNode *node = workspace_tree_.current();
  if (node == nullptr || node->type != TreeNodeType::CoreSection)
    return false;
  for (const ConfigFileEntry &entry : kCoreSections) {
    if (entry.id != node->id)
      continue;
    return entry.id == "core:keybindings" &&
           core_panel_->selectedIndex() == entry.section;
  }
  return false;
}

PlaybackCollection Application::browsedPlaybackCollection() const {
  PlaybackCollection browsed;
  switch (active_collection_) {
  case ActiveCollection::Library:
    browsed.kind = PlaybackCollection::Kind::Library;
    break;
  case ActiveCollection::History:
    browsed.kind = PlaybackCollection::Kind::History;
    break;
  case ActiveCollection::Playlist:
    browsed.kind = PlaybackCollection::Kind::Playlist;
    browsed.name = active_playlist_name_;
    break;
  }
  return browsed;
}

std::string Application::readOnlyReason() const {
  if (active_collection_ == ActiveCollection::History)
    return "History is generated from playback";
  if (active_collection_ == ActiveCollection::Playlist &&
      active_playlist_index_ == 0)
    return "Default mirrors the media library";
  if (active_collection_ == ActiveCollection::Playlist &&
      active_playlist_index_ < 0)
    return "This is not a saved playlist";
  return "Collection is read-only";
}

void Application::visualRange(int &lo, int &hi) const {
  const int last = std::max(0, static_cast<int>(activeTracks().size()) - 1);
  const int a = std::clamp(visual_anchor_, 0, last);
  const int b = std::clamp(track_cursor_, 0, last);
  lo = std::min(a, b);
  hi = std::max(a, b);
}

void Application::enterVisualSelection() {
  const std::vector<Song> &songs = activeTracks();
  if (songs.empty())
    return;
  // Visual selection is a Track Buffer concept only: entering it also pins the
  // pane, so the cursor cannot wander into the Tree mid-selection.
  workspace_pane_ = WorkspacePane::TrackList;
  vim_mode_ = VimMode::Visual;
  visual_anchor_ = std::clamp(track_cursor_, 0,
                              static_cast<int>(songs.size()) - 1);
  visual_list_size_ = songs.size();
  visual_message_.clear();
}

void Application::cancelVisualSelection() {
  vim_mode_ = VimMode::Normal;
  visual_anchor_ = -1;
  visual_list_size_ = 0;
  // The track cursor is deliberately left where it is.
}

void Application::yankVisualSelection() {
  const std::vector<Song> &songs = activeTracks();
  if (songs.empty()) {
    cancelVisualSelection();
    return;
  }
  int lo = 0;
  int hi = 0;
  visualRange(lo, hi);
  std::vector<TrackRef> refs;
  refs.reserve(static_cast<std::size_t>(hi - lo + 1));
  // Always natural buffer order, whatever direction the selection was made in.
  for (int index = lo; index <= hi; ++index)
    refs.push_back(trackRefFromSong(songs[static_cast<std::size_t>(index)]));
  const std::size_t count = refs.size();
  state_.music_register.set(std::move(refs));
  cancelVisualSelection();
  visual_message_ = std::to_string(count) +
                    (count == 1 ? " track yanked" : " tracks yanked");
}

void Application::yankCurrentTrack() {
  const std::vector<Song> &songs = activeTracks();
  if (songs.empty())
    return;
  const int index =
      std::clamp(track_cursor_, 0, static_cast<int>(songs.size()) - 1);
  state_.music_register.set({trackRefFromSong(songs[static_cast<std::size_t>(index)])});
  visual_message_ = "1 track yanked";
}

bool Application::textEntryActive() const {
  return playlist_prompt_ || search_prompt_;
}

bool Application::songMatches(const Song &song,
                              const std::string &needle) const {
  if (needle.empty())
    return false;
  const auto fold = [](std::string value) {
    // ASCII case folding only: multi-byte scripts have no case to fold, and
    // this keeps the match allocation-free and locale independent.
    for (char &c : value) {
      if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
    }
    return value;
  };
  const std::string pattern = fold(needle);
  return fold(song.displayTitle()).find(pattern) != std::string::npos ||
         fold(song.displayArtist()).find(pattern) != std::string::npos ||
         fold(song.album).find(pattern) != std::string::npos;
}

int Application::findMatch(int from, int direction) const {
  const std::vector<Song> &songs = activeTracks();
  const int count = static_cast<int>(songs.size());
  if (count == 0 || search_pattern_.empty() || direction == 0)
    return -1;
  for (int step = 1; step <= count; ++step) {
    const int index = ((from + direction * step) % count + count) % count;
    if (songMatches(songs[static_cast<std::size_t>(index)], search_pattern_))
      return index;
  }
  return -1;
}

void Application::openSearchPrompt() {
  search_prompt_ = true;
  search_typing_.store(true);
  visual_message_.clear();
  // A fresh query. The previous pattern stays available to `n` / `N` if the
  // user cancels, but it is not prefilled into the new one.
  search_buffer_.clear();
  search_no_match_ = false;
  // The box filters the RIGHT pane's list -- there is one scope and no target
  // to choose. The keyboard moves to that list (the box owns it, but the pane
  // is what the result cursor belongs to), and the Tree is left exactly as it
  // was: same cursor, same expansion, same active collection.
  search_anchor_ = track_cursor_;
  workspace_pane_ = WorkspacePane::TrackList;
  rebuildSearchRows();
  screen_.PostEvent(Event::Custom);
}

void Application::rebuildSearchRows() {
  search_rows_.clear();
  search_cursor_ = 0;
  search_scroll_ = 0;
  const std::vector<Song> &songs = activeTracks();
  if (search_buffer_.empty()) {
    // An empty query is not a filter: the complete list stays visible while the
    // box is still open and still typing.
    search_no_match_ = false;
    return;
  }
  // The matcher reads `search_pattern_`; the live query is used without
  // disturbing the accepted one that `n` / `N` walk.
  const std::string accepted = search_pattern_;
  search_pattern_ = search_buffer_;
  for (std::size_t index = 0; index < songs.size(); ++index) {
    if (songMatches(songs[index], search_pattern_))
      search_rows_.push_back(static_cast<int>(index));
  }
  search_pattern_ = accepted;
  search_no_match_ = search_rows_.empty();
  // Start on the first result at or after where the list cursor was, so a
  // search from the middle of a long list does not jump backwards; when there
  // is none, the first result.
  for (std::size_t view = 0; view < search_rows_.size(); ++view) {
    if (search_rows_[view] >= search_anchor_) {
      search_cursor_ = static_cast<int>(view);
      break;
    }
  }
}

int Application::songIndexAt(int view_index) const {
  if (searchActive() && view_index >= 0 &&
      view_index < static_cast<int>(search_rows_.size()))
    return search_rows_[static_cast<std::size_t>(view_index)];
  return view_index;
}

void Application::moveSearchCursor(int delta) {
  const int count = static_cast<int>(search_rows_.size());
  if (count <= 0 || delta == 0)
    return;
  search_cursor_ = std::clamp(search_cursor_ + delta, 0, count - 1);
  screen_.PostEvent(Event::Custom);
}

void Application::jumpToSearchResult(int view_index) {
  // The result knows WHICH occurrence it is: the view stores original row
  // indices, so duplicate titles can never confuse the two. Nothing else
  // changes -- no play, no queue, no context, no collection.
  if (view_index < 0 || view_index >= static_cast<int>(search_rows_.size())) {
    screen_.PostEvent(Event::Custom);
    return;
  }
  track_cursor_ = search_rows_[static_cast<std::size_t>(view_index)];
  search_prompt_ = false;
  search_typing_.store(false);
  search_rows_.clear();
  search_no_match_ = false;
  search_pattern_ = search_buffer_;
  search_buffer_.clear();
  workspace_pane_ = WorkspacePane::TrackList;
  // The viewport follows the cursor in renderTrackBuffer, so the original row
  // is revealed without a second scroll rule.
  screen_.PostEvent(Event::Custom);
}

void Application::closeSearchPrompt() {
  search_prompt_ = false;
  search_typing_.store(false);
  search_rows_.clear();
  search_no_match_ = false;
  search_buffer_.clear();
  // Esc is a cancel: the list cursor goes back to where the box opened.
  track_cursor_ = search_anchor_;
  workspace_pane_ = WorkspacePane::TrackList;
  screen_.PostEvent(Event::Custom);
}

bool Application::handleSearchPromptKey(const Event &event) {
  if (event == Event::Escape) {
    // Cancel: the complete list comes back and the cursor returns to where the
    // box opened. The accepted pattern stays usable for `n` / `N`.
    closeSearchPrompt();
    return true;
  }
  if (event == Event::Return) {
    jumpToSearchResult(search_cursor_);
    return true;
  }
  // Up/Down walk the RESULTS while the editor keeps every other key, so a `j`
  // or an `n` typed into the query is text and never a list command.
  if (event == Event::ArrowUp) {
    moveSearchCursor(-1);
    return true;
  }
  if (event == Event::ArrowDown) {
    moveSearchCursor(1);
    return true;
  }
  // Everything else belongs to the editor: FTXUI's Input edits UTF-8 by
  // codepoint (backspace, Delete, arrows, Home/End), which is what keeps a
  // Chinese query intact. `on_change` re-runs the filter.
  (void)search_input_->OnEvent(event);
  screen_.PostEvent(Event::Custom);
  return true;
}

void Application::autoLoadTreeCursor() {
  const TreeNode *node = workspace_tree_.current();
  if (node == nullptr)
    return;
  // Only real collections drive the Track Buffer. Moving onto the Playlists
  // group deliberately leaves the previous content in place rather than
  // emptying the buffer -- and, just as important, moving the TREE cursor never
  // moves the KEYBOARD: `j j j` has to be able to walk past a collection.
  switch (node->type) {
  case TreeNodeType::Database:
  case TreeNodeType::History:
  case TreeNodeType::Playlist:
  case TreeNodeType::CoreSection:
    loadTreeNode();
    break;
  case TreeNodeType::Group:
  case TreeNodeType::VaultRoot:
  case TreeNodeType::CoreRoot:
    break;
  }
}

int Application::treePlaylistIndex() const {
  const TreeNode *node = workspace_tree_.current();
  if (node == nullptr || node->type != TreeNodeType::Playlist)
    return -1;
  for (std::size_t index = 0; index < state_.library.playlists.size(); ++index) {
    if (state_.library.playlists[index] == node->label)
      return static_cast<int>(index);
  }
  return -1;
}

bool Application::collectionWritable() const {
  // Only a SAVED playlist accepts entry edits. Library is the media database
  // and History is generated from playback -- an edit to either would destroy a
  // source file or write to something the user did not select. A saved playlist
  // is user-owned. Index 0 is the virtual Default mirror and remains read-only.
  return active_collection_ == ActiveCollection::Playlist &&
         active_playlist_index_ > 0;
}

int Application::deleteRange(int lo, int hi) {
  const std::vector<Song> &songs = activeTracks();
  const int count = static_cast<int>(songs.size());
  if (count == 0)
    return 0;
  lo = std::max(0, lo);
  hi = std::min(hi, count - 1);
  if (lo > hi)
    return 0;

  if (!collectionWritable())
    return 0; // read-only collection
  std::vector<unsigned> positions;
  positions.reserve(static_cast<std::size_t>(hi - lo + 1));
  for (int index = lo; index <= hi; ++index)
    positions.push_back(static_cast<unsigned>(index));
  const int removed =
      controller_.removePlaylistEntries(active_playlist_name_, positions);
  if (removed > 0 && active_playlist_index_ >= 0) {
    controller_.selectPlaylist(active_playlist_index_);
    const int after = static_cast<int>(activeTracks().size());
    track_cursor_ = std::max(0, std::min(lo, after - 1));
  }
  return removed;
}

void Application::deleteVisualSelection() {
  if (active_collection_ == ActiveCollection::History) {
    int lo = 0;
    int hi = 0;
    visualRange(lo, hi);
    deleteHistoryRows(lo, hi);
    return;
  }
  if (!collectionWritable()) {
    cancelVisualSelection();
    visual_message_ = readOnlyReason();
    screen_.PostEvent(Event::Custom);
    return;
  }
  int lo = 0;
  int hi = 0;
  visualRange(lo, hi);
  const int removed = deleteRange(lo, hi);
  cancelVisualSelection();
  visual_message_ = removed > 0
                        ? "Removed " + std::to_string(removed) + " tracks"
                        : "Failed to remove tracks";
  screen_.PostEvent(Event::Custom);
}

void Application::deleteHistoryRows(int lo, int hi) {
  const std::vector<Song> &rows = activeTracks();
  if (rows.empty())
    return;
  lo = std::clamp(lo, 0, static_cast<int>(rows.size()) - 1);
  hi = std::clamp(hi, 0, static_cast<int>(rows.size()) - 1);
  if (hi < lo)
    std::swap(lo, hi);

  // Collect the RECORD ids of the selected rows. Ids, not indices and not
  // URIs: the store may hold the same URI several times and only the selected
  // occurrences may disappear.
  std::vector<long long> ids;
  ids.reserve(static_cast<std::size_t>(hi - lo + 1));
  for (int row = lo; row <= hi; ++row) {
    const long long id = rows[static_cast<std::size_t>(row)].history_record_id;
    if (id != 0)
      ids.push_back(id);
  }
  // Visual mode always ends with the edit, whether or not anything matched.
  cancelVisualSelection();
  if (ids.empty()) {
    visual_message_ = "Nothing to remove";
    screen_.PostEvent(Event::Custom);
    return;
  }
  const int removed = controller_.removeHistoryRecords(ids);
  if (removed < 0) {
    visual_message_ = "Removed from the view, but the history file could not be written";
    visual_message_error_ = true;
  } else if (removed == 0) {
    visual_message_ = "Nothing to remove";
  } else {
    visual_message_ = "Removed " + std::to_string(removed) +
                      (removed == 1 ? " history entry" : " history entries");
  }
  // The view is rebuilt from the store, so the cursor is re-clamped against the
  // rows that actually survived.
  refreshHistoryView();
  track_cursor_ = std::clamp(
      track_cursor_, 0,
      std::max(0, static_cast<int>(activeTracks().size()) - 1));
  visual_message_error_ = removed < 0;
  screen_.PostEvent(Event::Custom);
}

void Application::deleteCurrentEntry() {
  if (activeTracks().empty())
    return;
  // History is the one non-playlist collection that accepts a delete: its rows
  // are application-owned RECORDS, so removing one edits nothing but this
  // application's own history file. Library's rows are the media database and
  // `default` mirrors it, so both stay read-only.
  if (active_collection_ == ActiveCollection::History) {
    deleteHistoryRows(track_cursor_, track_cursor_);
    return;
  }
  if (!collectionWritable()) {
    visual_message_ = readOnlyReason();
    screen_.PostEvent(Event::Custom);
    return;
  }
  const int index = std::clamp(track_cursor_, 0,
                               static_cast<int>(activeTracks().size()) - 1);
  const int removed = deleteRange(index, index);
  visual_message_ = removed > 0 ? "Removed 1 track" : "Failed to remove track";
  screen_.PostEvent(Event::Custom);
}

bool Application::handlePlaylistPromptKey(const Event &event) {
  if (event == Event::Escape) {
    cancelPlaylistPrompt();
    return true;
  }
  if (event == Event::Return) {
    commitPlaylistPrompt();
    return true;
  }
  // Backspace edits the name here; it must never reach the pane chords.
  if (event == Event::Backspace) {
    if (!playlist_prompt_text_.empty())
      playlist_prompt_text_.pop_back();
    screen_.PostEvent(Event::Custom);
    return true;
  }
  if (event.is_character()) {
    // As above: accept the whole codepoint, not just single-byte input.
    const std::string typed = event.character();
    if (!typed.empty() &&
        static_cast<int>(static_cast<unsigned char>(typed[0])) >= 0x20)
      playlist_prompt_text_ += typed;
    screen_.PostEvent(Event::Custom);
    return true;
  }
  return true; // the prompt owns every other key while it is open
}

void Application::cancelPlaylistPrompt() {
  playlist_prompt_ = false;
  playlist_prompt_text_.clear();
  visual_message_.clear();
  screen_.PostEvent(Event::Custom);
}

void Application::commitPlaylistPrompt() {
  const std::string name = playlist_prompt_text_;
  const bool renaming = playlist_prompt_rename_;
  const std::string original = rename_original_;
  playlist_prompt_ = false;
  playlist_prompt_rename_ = false;
  rename_original_.clear();
  playlist_prompt_text_.clear();
  if (renaming) {
    const int index = treePlaylistIndex();
    if (index <= 0) {
      visual_message_ = index == 0 ? "Default cannot be renamed"
                                   : "Not a saved playlist";
      visual_message_error_ = true;
      screen_.PostEvent(Event::Custom);
      return;
    }
    if (name.empty()) {
      visual_message_ = "Playlist name required";
      screen_.PostEvent(Event::Custom);
      return;
    }
    if (name == "Library" || isDefaultPlaylistName(name)) {
      visual_message_ = "Playlist name is reserved";
      visual_message_error_ = true;
      screen_.PostEvent(Event::Custom);
      return;
    }
    controller_.selectPlaylist(index);
    if (!controller_.renameCurrentPlaylist(name)) {
      visual_message_ = "Failed to rename playlist";
    } else {
      visual_message_ = "Renamed to \"" + name + "\"";
      if (active_playlist_name_ == original)
        active_playlist_name_ = name;
    }
    screen_.PostEvent(Event::Custom);
    return;
  }
  if (name.empty()) {
    visual_message_ = "Playlist name required";
    screen_.PostEvent(Event::Custom);
    return;
  }
  if (name == "Library" || isDefaultPlaylistName(name)) {
    visual_message_ = "Playlist name is reserved";
    visual_message_error_ = true;
    screen_.PostEvent(Event::Custom);
    return;
  }
  const std::size_t queued = state_.music_register.size();
  if (!controller_.createPlaylist(name)) {
    // MPD saves the current queue to seed a playlist, so an empty queue cannot
    // produce one. Say what the user can actually do about it.
    visual_message_ = state_.music_register.empty()
                          ? "Yank tracks first"
                          : "Failed to create playlist";
    screen_.PostEvent(Event::Custom);
    return;
  }
  // A register that is already loaded becomes the new playlist's contents, so
  // `v j j y` then `a Night Drive` produces a real playlist with those tracks.
  int added = 0;
  if (queued > 0)
    added = controller_.pasteRegisterToPlaylist(name);
  visual_message_ =
      added > 0 ? "Created \"" + name + "\" with " + std::to_string(added) +
                      " tracks"
                : "Created playlist \"" + name + "\"";
  screen_.PostEvent(Event::Custom);
}

void Application::pasteRegisterToTreeSelection() {
  if (state_.music_register.empty()) {
    visual_message_ = "Nothing yanked";
    return;
  }
  const TreeNode *node = workspace_tree_.current();
  if (node == nullptr)
    return;
  if (node->type == TreeNodeType::Group) {
    visual_message_ = "Cannot paste into a group";
    visual_message_error_ = true;
    return;
  }
  if (node->type == TreeNodeType::Playlist) {
    if (isDefaultPlaylistName(node->id)) {
      visual_message_ = "Default mirrors the media library";
      visual_message_error_ = true;
      return;
    }
    const int added = controller_.pasteRegisterToPlaylist(node->id);
    visual_message_ = added > 0 ? "Added " + std::to_string(added) +
                                      " tracks to \"" + node->label + "\""
                                : "Failed to add tracks";
    // MPD now holds more entries than the snapshot the Track Buffer was built
    // from. Reload when the pasted playlist is the one on screen, otherwise
    // the view (and every edit that indexes it) works from a stale list.
    if (added > 0 && active_collection_ == ActiveCollection::Playlist &&
        active_playlist_name_ == node->label)
      controller_.selectPlaylist(active_playlist_index_);
    return;
  }
  // Library and History are generated/read-only views.
  visual_message_ = node->type == TreeNodeType::History
                        ? "History is generated from playback"
                        : "Collection is read-only";
  visual_message_error_ = true;
}

bool Application::handleWorkspaceKey(const Event &event) {
  // Superseded by the configurable keymap: every key this function used to
  // decode (j/k/h/l, v, y, d, a, p, gg/G, Ctrl+d/u, Enter, the chords) is now
  // a keymap binding. Leaving it live made a rebound key fall through to its
  // old hard-coded meaning, so customisation could never actually take effect.
  (void)event;
  return false;
}

bool Application::handleWorkspaceMouse(const Mouse &mouse) {
  if (state_.page != Page::Library || mouse.button != Mouse::Left)
    return false;
  if (mouse.motion != Mouse::Pressed)
    return false;

  // Track rows first: while the search box is open, clicking a RESULT selects
  // it -- the same jump Enter performs, never playback.
  if (search_prompt_) {
    for (std::size_t slot = 0; slot < track_row_boxes_.size(); ++slot) {
      if (track_row_boxes_[slot].IsEmpty() ||
          !track_row_boxes_[slot].Contain(mouse.x, mouse.y))
        continue;
      const int original = track_row_index_[slot];
      for (std::size_t view = 0; view < search_rows_.size(); ++view) {
        if (search_rows_[view] != original)
          continue;
        jumpToSearchResult(static_cast<int>(view));
        return true;
      }
      return true;
    }
    // The box owns the keyboard: a click behind it must not move the Tree
    // while a query is being typed.
    return true;
  }

  // Tree rows: real final-layout hitboxes.
  const auto &nodes = workspace_tree_.visible();
  for (std::size_t index = 0;
       index < tree_boxes_.size() && index < nodes.size(); ++index) {
    if (tree_boxes_[index].IsEmpty() ||
        !tree_boxes_[index].Contain(mouse.x, mouse.y))
      continue;
    workspace_pane_ = WorkspacePane::Tree;
    while (workspace_tree_.cursor() < static_cast<int>(index))
      workspace_tree_.moveCursor(1);
    while (workspace_tree_.cursor() > static_cast<int>(index))
      workspace_tree_.moveCursor(-1);
    last_click_track_ = -1;
    autoLoadTreeCursor();
    screen_.PostEvent(Event::Custom);
    return true;
  }

  // Track rows: per-row hitboxes, so no manual row arithmetic.
  for (std::size_t slot = 0; slot < track_row_boxes_.size(); ++slot) {
    if (track_row_boxes_[slot].IsEmpty() ||
        !track_row_boxes_[slot].Contain(mouse.x, mouse.y))
      continue;
    workspace_pane_ = WorkspacePane::TrackList;
    track_cursor_ = track_row_index_[slot];
    const auto now = std::chrono::steady_clock::now();
    if (last_click_track_ == track_cursor_ &&
        now - last_click_ < std::chrono::milliseconds(400)) {
      last_click_ = {};
      last_click_track_ = -1;
      (void)performKeymapAction(Action::Activate);
    } else {
      last_click_ = now;
      last_click_track_ = track_cursor_;
    }
    screen_.PostEvent(Event::Custom);
    return true;
  }
  return false;
}

Element Application::renderTreePane() {
  syncTreeFromPlaylists();
  const auto &nodes = workspace_tree_.visible();
  const int inner = std::max(8, metrics_.workspace_tree_width - 2);

  // The pink band is the FOCUS marker: the tree draws it only while the tree
  // owns the keyboard, and an overlay in the bottom row takes that ownership
  // away from every pane.
  const bool tree_focused =
      workspace_pane_ == WorkspacePane::Tree && !overlayOwnsKeyboard();
  tree_boxes_.assign(nodes.size(), Box{});
  const int cursor = workspace_tree_.cursor();
  // The viewport is the metrics' number, not a guess: the rows below it belong
  // to the sidebar's playback block.
  const int viewport = std::max(1, metrics_.sidebar.tree_rows);
  if (cursor < tree_scroll_)
    tree_scroll_ = cursor;
  if (cursor >= tree_scroll_ + viewport)
    tree_scroll_ = cursor - viewport + 1;
  const int last = std::min(static_cast<int>(nodes.size()),
                            tree_scroll_ + viewport);
  tree_scroll_ = std::clamp(tree_scroll_, 0,
                            std::max(0, static_cast<int>(nodes.size()) - viewport));

  Elements rows;
  for (int index = tree_scroll_; index < last; ++index) {
    const TreeNode &node = nodes[static_cast<std::size_t>(index)];
    const bool is_cursor = index == cursor;
    // The ACTIVE collection is independent of the cursor: identity comes from
    // the node type and active playlist name, not the cursor row.
    const bool is_saved_playlist_active =
        node.type == TreeNodeType::Playlist &&
        active_collection_ == ActiveCollection::Playlist &&
        active_playlist_name_ == node.id;
    const bool is_active =
        (node.type == TreeNodeType::History &&
         active_collection_ == ActiveCollection::History) ||
        (node.type == TreeNodeType::Database &&
         active_collection_ == ActiveCollection::Library) ||
        is_saved_playlist_active;

    // Three different states, three different colours: the active collection
    // is a restrained cyan dot, the Tree cursor is a pink row, and everything
    // else is muted. The cursor never hides which collection is open.
    const bool directory = node.type == TreeNodeType::Group ||
                           node.type == TreeNodeType::VaultRoot ||
                           node.type == TreeNodeType::CoreRoot;
    // FILESYSTEM, not a form: the tree carries no disclosure triangle, no
    // round marker and no leading gutter. Indentation plus the node's own icon
    // say where a row sits, and the open collection is told apart by its
    // COLOUR (`active_collection`) and by the cursor band -- never by a symbol.
    // One margin cell keeps the icons off the frame; each level adds two.
    std::string prefix = " ";
    prefix += std::string(static_cast<std::size_t>(node.depth) * 2U, ' ');

    // One icon per node KIND, chosen from the model rather than from the label:
    // a saved playlist called "Library" is still a playlist.
    Icon node_icon = Icon::Music;
    bool has_icon = true;
    switch (node.type) {
    case TreeNodeType::VaultRoot:
    case TreeNodeType::CoreRoot:
    case TreeNodeType::Group:
      // Directories follow one rule everywhere, roots included: an open folder
      // when expanded, a closed one when not. There is no separate "root" icon
      // and no triangle -- the folder and its visible children say it all.
      node_icon = node.expanded ? Icon::FolderOpen : Icon::Folder;
      break;
    case TreeNodeType::Database:
      node_icon = Icon::Library;
      break;
    case TreeNodeType::History:
      node_icon = Icon::History;
      break;
    case TreeNodeType::Playlist:
      // A saved playlist: a list icon. Every playlist row IS a saved playlist,
      // so there is no second kind to tell it apart from.
      node_icon = Icon::Playlist;
      break;
    case TreeNodeType::CoreSection:
      // Two kinds, two icons: a `core` entry that only shows text is a
      // document, one that can be changed is a gear. The KIND comes from the
      // node -- the renderer never looks a section up by name.
      node_icon = node.core_kind == CoreSectionKind::Information
                      ? Icon::Document
                      : Icon::Settings;
      break;
    }

    const std::string icon =
        has_icon ? iconGlyph(node_icon, icon_set_) + " " : std::string("  ");
    const int prefix_width = util::displayWidth(prefix);
    const int label_width =
        std::max(4, inner - prefix_width - kContentIconWidth);
    const std::string label =
        util::padRight(util::ellipsize(node.label, label_width), label_width);

    // Three elements: the leading state slot, the icon in its own restrained
    // tone, and the label. The cursor's background is applied to the whole row
    // below, so the icon is highlighted WITH its text rather than separately.
    // Icon colour: the two `core` kinds carry the restrained accent that says
    // which they are, everything else keeps the quiet tree tones. On the cursor
    // row the icon takes the CURSOR colour like the label does, so the pink
    // band is one row and not "bright icon + highlighted text".
    const bool on_cursor_row = tree_focused && is_cursor;
    Color icon_color = theme_.weak_text;
    if (!has_icon) {
      icon_color = theme_.weak_text;
    } else if (node.type == TreeNodeType::CoreSection) {
      icon_color = node.core_kind == CoreSectionKind::Information
                       ? theme_.accent_purple  // Lavender: information
                       : theme_.accent_primary; // Mauve: configurable
    } else {
      icon_color = directory ? theme_.weak_text : theme_.muted_text;
    }
    if (on_cursor_row)
      icon_color = theme_.tree_cursor_fg;
    Element row = hbox({
        text(prefix),
        text(icon) | color(icon_color),
        text(label),
        filler(),
    });
    // ONE strong highlight on screen at a time. The pink row is the Tree's
    // focus marker, so it is painted only while the Tree owns the keyboard.
    // An unfocused Tree shows its cursor row with the ordinary row styling --
    // the active collection's dot and colour still say what is open.
    if (tree_focused && is_cursor)
      row = row | color(theme_.tree_cursor_fg) | bgcolor(theme_.tree_cursor_bg);
    else if (node.type == TreeNodeType::VaultRoot ||
             node.type == TreeNodeType::CoreRoot)
      // The two top-level directories are the anchors of the whole tree: they
      // alone take the primary magenta.
      row = row | bold | color(theme_.accent_primary);
    else if (is_active)
      // The open collection keeps its own state colour, so "where am I" is
      // never confused with "what kind of row is this".
      row = row | color(theme_.active_collection);
    else
      // Everything inside the directories is the lighter pink-purple.
      row = row | color(theme_.tree_item);
    rows.push_back(std::move(row) |
                   reflect(tree_boxes_[static_cast<std::size_t>(index)]));
  }
  // No focus tint: the Tree cursor already says which pane has the keyboard,
  // and a coloured frame made the layout compete with its own content.
  // No filler either: the Sidebar composes the tree, the free space and the
  // playback block, so the block is anchored to the bottom by the LAYOUT.
  return vbox(std::move(rows));
}

Element Application::renderSidebarPlayback() {
  // The sidebar's lower region: the now-playing anchor.
  //
  // FOUR lines, built from the BOTTOM UP so the block reads as a hierarchy:
  //
  //   ┃ <artist>        faintest   weak_text + dim
  //   ┃ <title>         body        text + bold
  //   ┃ # <context>     accent      accent_secondary
  //   ┃ NOW PLAYING     anchor      accent_primary + bold
  //
  // "Fainter as you go up": the top line is the least important, the bottom one
  // is the block's anchor. The context line is the OWNER of the playback run
  // (`playbackContext()`, the same authority the playing marker uses) -- never
  // the browsed collection, so browsing History while a `default` run plays
  // keeps showing `# default`.
  //
  // Display-only: not focusable, not selectable, not searchable, not clickable,
  // and it carries no hint text. It never reports time, progress, volume,
  // transport state, album, format or bitrate.
  const SidebarLayout &side = metrics_.sidebar;
  sidebar_roles_.clear();
  sidebar_bar_.clear();
  if (side.playback_rows <= 0)
    return text("");

  const Song *song =
      state_.player.current_song ? &*state_.player.current_song : nullptr;
  if (song == nullptr) {
    // Nothing playing: the block is HIDDEN. No stale metadata, no empty bar, no
    // placeholder song. The rows stay reserved so starting or stopping playback
    // never moves the tree.
    return blankRows(side.playback_rows);
  }

  // One row per line, width-aware truncation: CJK and Nerd Font glyphs count as
  // the cells they occupy, never as bytes. The bar and its one-cell gap are
  // budgeted first, so a line can never be wider than the column it is drawn in
  // and therefore never wraps.
  const int width = std::max(4, side.width);
  const int text_width = std::max(1, width - 2);
  const auto line = [text_width](const std::string &value) {
    return util::ellipsize(value, text_width);
  };

  // The playback context, named by its own kind. `None` means the run was not
  // started from a collection this application owns, and the block then shows
  // the song without inventing an owner.
  std::string context;
  switch (controller_.playbackContext().kind) {
  case PlaybackCollection::Kind::Library:
    context = "Library";
    break;
  case PlaybackCollection::Kind::History:
    context = "History";
    break;
  case PlaybackCollection::Kind::Playlist:
    context = controller_.playbackContext().name;
    break;
  case PlaybackCollection::Kind::None:
    break;
  }
  if (!context.empty())
    context = "# " + context;

  // The four candidate lines, in VISUAL order (faintest first). `rank` is how
  // long a line survives when the sidebar is too short: the anchor outlives
  // everything, the title outlives the context, and the artist -- the faintest
  // line -- is the first to go. Keeping the N highest ranks and then re-sorting
  // them visually is what makes the degradation order explicit instead of an
  // accident of the list order.
  struct BlockLine {
    std::string text;
    std::string role; ///< "colour:weight", reported to the scripted harness
    int rank = 0;
    ftxui::Element element;
  };
  std::vector<BlockLine> candidates;
  candidates.push_back({song->displayArtist(), "weak_text:dim", 1,
                        text(" " + line(song->displayArtist())) | dim |
                            color(theme_.weak_text)});
  candidates.push_back({song->displayTitle(), "text:bold", 3,
                        text(" " + line(song->displayTitle())) | bold |
                            color(theme_.text)});
  if (!context.empty())
    candidates.push_back({context, "accent_secondary:regular", 2,
                          text(" " + line(context)) |
                              color(theme_.accent_secondary)});
  candidates.push_back({"NOW PLAYING", "accent_primary:bold", 4,
                        text(" " + line("NOW PLAYING")) | bold |
                            color(theme_.accent_primary)});

  std::vector<BlockLine> lines;
  for (BlockLine &candidate : candidates) {
    if (side.playback_rows >= 4 ||
        (side.playback_rows == 3 && candidate.rank >= 2) ||
        (side.playback_rows == 2 && candidate.rank >= 3))
      lines.push_back(std::move(candidate));
  }

  // The accent bar: ONE cell, an existing theme accent, static (it does not
  // follow playback, the beat or the cursor) and never interactive. It spans
  // every drawn line, with one cell of air before the text.
  const std::string kBar = "\u2503";
  sidebar_bar_ = "accent_primary";
  Elements block;
  for (const BlockLine &line_value : lines) {
    sidebar_roles_.push_back(line_value.role);
    block.push_back(hbox({
        text(kBar) | color(theme_.accent_primary),
        std::move(line_value.element),
        filler(),
    }));
  }
  // The reserved rows the block did not need stay blank, and NOW PLAYING ends
  // on the sidebar's last row: no gap above the block, no padding below it.
  for (int index = static_cast<int>(lines.size());
       index < side.playback_rows; ++index)
    block.push_back(text(""));
  return vbox(std::move(block));
}

Element Application::renderTrackBuffer() {
  const std::vector<Song> &songs = activeTracks();
  const int inner = std::max(16, metrics_.track_buffer_width - 2);
  // The VIEW: the complete list, or the rows a live query matched. Only the
  // positions change -- every row still resolves to its ORIGINAL index through
  // songIndexAt(), so the playing marker, a click and the jump target all use
  // identity instead of a title that may repeat.
  const bool filtering = searchActive();
  const int view_size = filtering ? static_cast<int>(search_rows_.size())
                                  : static_cast<int>(songs.size());
  int &cursor = filtering ? search_cursor_ : track_cursor_;
  int &scroll = filtering ? search_scroll_ : track_scroll_;

  // The active collection's column budget. Library rows carry a file size,
  // History rows the moment the track played, playlist rows the tags MPD
  // reports for a saved playlist -- one layout each, chosen here and nowhere
  // else, so a row and its header can never disagree.
  const TrackColumns &columns = activeColumns();

  Song heading;
  heading.title = "Title";
  heading.artist = "Artist";
  heading.album = "Album";
  heading.duration_seconds = -1.0;

  // Invariant: the Box storage reaches its FINAL size before any
  // reflect(Box&) decorator is constructed. reflect() captures a reference,
  // so a later push_back/resize/assign would dangle it (this caused a
  // std::bad_alloc crash in Round 32). Pre-size, then index -- never push
  // into a container that reflect() is already pointing at.
  Elements rows;
  // The collection name is the first row: it used to be the panel title, and
  // without it there is no way to tell which collection the buffer shows.
  // Library, History and `default` are named EXACTLY as the Tree names them,
  // so the media database can never be mistaken for the mirror playlist.
  const std::string collection =
      collectionLabel() + (filtering ? " \u2014 Search" : std::string());
  rows.push_back(text(" " + util::ellipsize(collection, inner)) | bold |
                 color(theme_.accent_primary));
  rows.push_back(text(songRow(heading, -1, inner, false)) |
                 color(theme_.header_text));
  rows.push_back(text(util::repeat(inner, "\u2500")) | color(theme_.border_dim));

  if (view_size == 0) {
    track_row_boxes_.assign(0, Box{});
    track_row_index_.assign(0, 0);
    rows.push_back(text(""));
    // A query that matches nothing shows NO list at all: an empty state, not
    // the unrelated rows that happened to be there before.
    rows.push_back(text(filtering ? "No matches." : "Collection is empty") |
                   color(filtering ? theme_.error : theme_.muted_text));
  } else {
    cursor = std::clamp(cursor, 0, view_size - 1);
    const int page = std::max(1, metrics_.main_height - 5);
    if (cursor < scroll)
      scroll = cursor;
    if (cursor >= scroll + page)
      scroll = cursor - page + 1;
    scroll = std::clamp(scroll, 0, std::max(0, view_size - page));
    const int visible_rows = std::clamp(view_size - scroll, 0, page);

    // Final size fixed here, before any Element is built.
    int visual_lo = -1;
    int visual_hi = -1;
    if (visualActive())
      visualRange(visual_lo, visual_hi);

    track_row_boxes_.assign(static_cast<std::size_t>(visible_rows), Box{});
    track_row_index_.assign(static_cast<std::size_t>(visible_rows), 0);
    for (int row = 0; row < visible_rows; ++row)
      track_row_index_[static_cast<std::size_t>(row)] =
          songIndexAt(scroll + row);

    // AT MOST ONE playing marker, resolved once for the whole buffer.
    //
    // The playing row belongs to the collection playback STARTED from, and to
    // the exact OCCURRENCE inside it. Two independent halves:
    //
    //   displayed collection == playback context   (otherwise: no marker)
    //   displayed row        == that occurrence    (by history record id for
    //                                               History, by duplicate-aware
    //                                               occurrence for a list)
    //
    // Matching on the URI alone is deliberately NOT enough: the same URI lives
    // in Library, in `default` and in several playlists at once, and History can
    // hold it twice -- so a URI match would mark rows that are not playing.
    const int playing_index = currentPlayingRow();

    for (int row = 0; row < visible_rows; ++row) {
      const int position = scroll + row;
      // The row's ORIGINAL index: identity, never a display string.
      const int index = songIndexAt(position);
      const Song &song = songs[static_cast<std::size_t>(index)];
      const bool is_playing = index == playing_index;
      // Four independent states, none of which may hide another:
      //   playing marker  -> Sky, always drawn, never a full-row wash
      //   Visual range    -> restrained Surface wash
      //   cursor row      -> Mauve background with Crust text
      //   normal hierarchy-> Text title, Subtext1 artist, Subtext0 album/time
      const bool in_visual =
          visualActive() && index >= visual_lo && index <= visual_hi;
      // Only the FOCUSED Track Buffer paints the strong cursor row. While the
      // Tree owns the keyboard this pane shows no highlight at all: the
      // playing row keeps its marker and colour, which is a hint, not a focus.
      // The cursor row: the list's own cursor normally; while the box is open
      // the RESULT cursor, which is what the user is choosing between.
      const bool is_cursor =
          filtering
              ? position == cursor
              : (index == track_cursor_ &&
                 workspace_pane_ == WorkspacePane::TrackList);
      // On the cursor row every glyph goes Crust so the Mauve highlight stays
      // readable; the Sky marker is the one element that keeps its own colour,
      // which is what keeps "playing" visible while the cursor sits on it.
      const Color title_fg =
          is_cursor ? theme_.track_cursor_fg
                    : (is_playing ? theme_.playing : theme_.text);
      const Color secondary_fg =
          is_cursor ? theme_.track_cursor_fg : theme_.header_text;
      const Color muted_fg =
          is_cursor ? theme_.track_cursor_fg : theme_.muted_text;
      // Results are renumbered inside the filtered view; the complete list
      // keeps the ordinals it always had.
      const SongRowParts parts = songRowParts(song, position + 1, columns);
      // Every cell is pinned to its exact column width. An hbox otherwise
      // redistributes space between flexible text nodes, which would silently
      // re-flow the columns; pinning keeps the row laid out exactly where the
      // metrics put it, and a row wider than the pane clips at the edge just
      // as the single-string version did.
      // The assembled row is clipped to the panel before it is split into
      // cells. FTXUI compresses EVERY child of an over-wide box, so handing it
      // a row it cannot fit would silently re-flow the columns; cutting the
      // row first keeps each column exactly where the metrics put it and lets
      // the tail clip at the panel edge, exactly as a single string did.
      int remaining = inner;
      Elements cells;
      cells.reserve(7);
      const auto cell = [&](std::string text_value, const Color &fg) {
        if (text_value.empty() || remaining <= 0)
          return;
        text_value = util::clipToWidth(text_value, remaining);
        const int width =
            std::max(1, static_cast<int>(util::displayWidth(text_value)));
        remaining -= width;
        cells.push_back(text(std::move(text_value)) | color(fg) |
                        size(WIDTH, EQUAL, width));
      };
      // "▶ " including the separating space, matching songRow(), so the
      // columns stay aligned with the header row.
      cell(is_playing ? "\u25b6 " : "  ", theme_.playing);
      cell(parts.ordinal, muted_fg);
      // The song-icon column: reserved on every row, so a row is exactly as
      // wide playing or not. The glyph is a CONTENT icon (this row is a song);
      // the playing marker above is the state, and the two never merge.
      cell(parts.icon, muted_fg);
      // The separator belongs to the title column, so a row with no artist
      // still starts its title at the same cell as every other row.
      cell(parts.title_lead + parts.title, title_fg);
      cell(parts.artist, secondary_fg);
      cell(parts.album, muted_fg);
      cell(parts.duration, muted_fg);
      cell(parts.size, muted_fg);
      cell(parts.played, muted_fg);
      Element line = hbox(std::move(cells));
      if (is_cursor)
        line = line | bgcolor(theme_.track_cursor_bg);
      else if (in_visual)
        line = line | bgcolor(theme_.visual_selection_bg);
      rows.push_back(std::move(line) |
                     reflect(track_row_boxes_[static_cast<std::size_t>(row)]));
    }
  }

  // Focus is communicated by the cursor row, never by tinting the pane: a
  // coloured frame on focus made the whole layout shout.
  return vbox(std::move(rows));
}

namespace {

/// A row of `total_width` cells with `content` centred in it. Written out with
/// explicit padding instead of a `center` decorator: alignment is not honoured
/// once `size(...)` is applied, and the immersive body depends on every column
/// landing exactly where the metrics say it does.
ftxui::Element centredRow(ftxui::Element content, int content_width,
                          int total_width) {
  const int pad = std::max(0, (total_width - content_width) / 2);
  if (pad == 0)
    return hbox({std::move(content), filler()});
  return hbox({text(util::repeat(pad)), std::move(content), filler()});
}

} // namespace

Element Application::renderImmersiveNowPlaying() {
  // Immersive is a MODE, not a page. It is a single centered column, sized by
  // `metrics_.immersive` -- this function places the bands and invents nothing:
  //
  //   +----------------------------- frame ------------------------------+
  //   |                  Nightfall            (centered, heavier)         |
  //   |                  Aurora Fields        (centered, lighter)         |
  //   |                              <- info_gap_rows (space, not a rule) |
  //   |                                                                   |
  //   |                    VISUALIZER  (the only primary content)         |
  //   |                                                                   |
  //   |                            <- player_gap_rows (space, no rule)    |
  //   |   ------------------ Player Bar (same frame) --------------------  |
  //   +-------------------------------------------------------------------+
  //
  // The screen is the song's identity and its sound, and nothing else: no
  // lyrics, no Album, Year, Genre, Format or Length (those stay in the model
  // and in the workspace table, where a question about a file belongs), and no
  // reserved slot for anything that might be missing.
  //
  // Nothing here reads the spectrum's height either: the visualizer's box is a
  // pure function of the terminal size (see `computeMetrics`), so an animating
  // signal can never reflow the layout.
  const Song *song =
      state_.player.current_song ? &*state_.player.current_song : nullptr;
  const std::string title = song ? song->displayTitle() : "No track playing";
  const std::string artist = song ? song->displayArtist() : "\u2014";
  const ImmersiveLayout &layout = metrics_.immersive;
  const int content_width = std::max(8, layout.content_width);
  const std::string margin = util::repeat(layout.outer_left_pad);

  // A. The information bar: two centered rows, title over artist. A cell has no
  //    font size, so the title's extra weight is carried the way a terminal
  //    can: bold, in the accent colour, while the artist stays regular and
  //    dimmed.
  const std::string heading = util::ellipsize(title, content_width - 2);
  const std::string subheading = util::ellipsize(artist, content_width - 2);
  Elements info;
  info.push_back(centredRow(text(heading) | bold | color(theme_.accent_primary),
                            util::displayWidth(heading), content_width));
  info.push_back(centredRow(text(subheading) | color(theme_.weak_text),
                            util::displayWidth(subheading), content_width));

  // B. The visualizer: the whole content width, and the visual region minus the
  //    pads the layout reserved above and below it.
  Elements center;
  center.push_back(blankRows(layout.visualizer_top_pad));
  // The same outer margin the information bar uses, and then the WHOLE drawable
  // width: the Spectrum spreads across its container and centres itself inside
  // it. The grid air the old chunky styles were inset by would only shrink the
  // spectrum by a fifth of the screen.
  center.push_back(
      hbox({text(margin), renderImmersiveVisualizer(), filler()}));
  center.push_back(blankRows(layout.visualizer_bottom_pad));

  return vbox({
      hbox({text(margin),
            vbox(std::move(info)) | size(HEIGHT, EQUAL, layout.info_rows),
            text(margin), filler()}),
      // Logical separation: blank ROWS, never a drawn line.
      blankRows(layout.info_gap_rows),
      vbox(std::move(center)),
      filler(),
  });
}

int Application::coreSectionIndex() const {
  return -1;
}

int Application::currentPlayingRow() const {
  return controller_.playingRow(browsedPlaybackCollection(), activeTracks());
}

std::pair<int, int> Application::liveSize() const {
  if (script_width_ > 0 && script_height_ > 0)
    return {script_width_, script_height_};
  return {std::max(1, screen_.dimx()), std::max(1, screen_.dimy())};
}

void Application::resizeScript(int width, int height) {
  script_width_ = std::max(1, width);
  script_height_ = std::max(1, height);
}

bool Application::scriptKey(std::string_view token) {
  if (root_ == nullptr || token.empty())
    return false;
  const auto named = [&](const Event &event) { return root_->OnEvent(event); };
  if (token == "Enter")
    return named(Event::Return);
  if (token == "Esc" || token == "Escape")
    return named(Event::Escape);
  if (token == "Tab")
    return named(Event::Tab);
  if (token == "Space" || token == " ")
    return named(Event::Character(' '));
  if (token == "Up")
    return named(Event::ArrowUp);
  if (token == "Down")
    return named(Event::ArrowDown);
  if (token == "Left")
    return named(Event::ArrowLeft);
  if (token == "Right")
    return named(Event::ArrowRight);
  // Navigation keys and control chords. Without these the harness silently
  // DROPPED the token, which made an assertion like "PageDown still works"
  // vacuous: the key was never delivered to the application at all.
  if (token == "PageUp")
    return named(Event::PageUp);
  if (token == "PageDown")
    return named(Event::PageDown);
  if (token == "Home")
    return named(Event::Home);
  if (token == "End")
    return named(Event::End);
  if (token == "Delete")
    return named(Event::Delete);
  if (token == "Backspace")
    return named(Event::Backspace);
  if (token == "Ctrl+d")
    return named(Event::CtrlD);
  if (token == "Ctrl+u")
    return named(Event::CtrlU);
  if (token == "Ctrl+w")
    return named(Event::CtrlW);
  if (token == "Ctrl+l")
    return named(Event::CtrlL);
  if (token == "Ctrl+h")
    return named(Event::Backspace);
  // A chord ("g g") is fed one event at a time, exactly as a keyboard does.
  if (const auto space = token.find(' '); space != std::string_view::npos) {
    bool handled = false;
    std::size_t offset = 0;
    while (offset <= token.size()) {
      const auto next = token.find(' ', offset);
      const std::string_view piece =
          token.substr(offset, next == std::string_view::npos
                                   ? std::string_view::npos
                                   : next - offset);
      if (!piece.empty())
        handled = scriptKey(piece) || handled;
      if (next == std::string_view::npos)
        break;
      offset = next + 1;
    }
    return handled;
  }
  if (token.size() == 1)
    return named(Event::Character(token.front()));
  // ONE UTF-8 codepoint (a Chinese character, an accent) is ONE character
  // event, exactly as an IME commit delivers it. Splitting it into bytes made
  // `type 枫` drop the token silently and every non-ASCII regression vacuous.
  if (scriptCodepoints(token).size() == 1)
    return named(Event::Character(std::string(token)));
  return false;
}

/// Splits text into one UTF-8 codepoint per string, which is what an FTXUI
/// character event carries. Invalid bytes are passed through one at a time so a
/// malformed script cannot loop forever.
std::vector<std::string> scriptCodepoints(std::string_view text) {
  std::vector<std::string> pieces;
  for (std::size_t offset = 0; offset < text.size();) {
    const auto lead = static_cast<unsigned char>(text[offset]);
    std::size_t length = 1;
    if ((lead & 0xE0U) == 0xC0U)
      length = 2;
    else if ((lead & 0xF0U) == 0xE0U)
      length = 3;
    else if ((lead & 0xF8U) == 0xF0U)
      length = 4;
    length = std::min(length, text.size() - offset);
    pieces.emplace_back(text.substr(offset, length));
    offset += length;
  }
  return pieces;
}

std::vector<std::string> Application::scriptFrame() {
  const auto [width, height] = liveSize();
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, root_->Render());
  std::vector<std::string> rows;
  rows.reserve(static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    std::string row;
    for (int x = 0; x < width; ++x) {
      const auto &pixel = screen.PixelAt(x, y);
      row += pixel.character.empty() ? " " : pixel.character;
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::string Application::scriptStateSummary() {
  const auto kind = [](PlaybackCollection::Kind k) {
    switch (k) {
    case PlaybackCollection::Kind::None: return "none";
    case PlaybackCollection::Kind::Library: return "library";
    case PlaybackCollection::Kind::History: return "history";
    case PlaybackCollection::Kind::Playlist: return "playlist";
    }
    return "?";
  };
  const PlaybackCollection browsed = browsedPlaybackCollection();
  const PlaybackCollection &context = controller_.playbackContext();
  const PlaybackSession &session = controller_.playbackSession();
  const int marker = currentPlayingRow();
  std::ostringstream out;
  out << "browsed=" << kind(browsed.kind)
      << (browsed.name.empty() ? "" : ":" + browsed.name)
      << " context=" << kind(context.kind)
      << (context.name.empty() ? "" : ":" + context.name)
      << " rows=" << activeTracks().size() << " marker=" << marker
      << " occ=" << session.occurrence
      << " seq=" << session.sequence.size()
      << " cursor=" << track_cursor_
      // The search box and its filtered view: `searchRows` is how many rows of
      // the RIGHT list matched, `searchCursor` the result cursor inside that
      // view, and `searchQuery` what is on the line. The list keeps reporting
      // its own `cursor` / `rows`, so a test can tell the view from the list.
      << " searchOn=" << (search_prompt_ ? 1 : 0)
      << " searchRows="
      << (searchActive() ? static_cast<int>(search_rows_.size()) : 0)
      << " searchCursor=" << (searchActive() ? search_cursor_ : 0)
      << " searchQuery="
      << (search_prompt_ ? search_buffer_ : std::string())
      << " pane="
      << (workspace_pane_ == WorkspacePane::Tree ? "tree" : "tracks")
      // Single-pane mode hides the sidebar (and with it the playback block):
      // a script has to be able to tell "no block because no sidebar" from
      // "block missing".
      << " single=" << (metrics_.workspace_single_pane ? 1 : 0)
      // The tree's own cursor, by STABLE ID: a test can then name what is
      // selected instead of counting rows, which is what keeps it honest when
      // the tree gains or loses an entry.
      << " node="
      << (workspace_tree_.current() != nullptr ? workspace_tree_.current()->id
                                               : "-")
      << " nodeRow=" << workspace_tree_.cursor()
      // How many rows the tree currently shows. A fold is a VISIBILITY change,
      // so this is what a test asserts on: the cursor can stay on the same node
      // while the row count changes underneath it.
      << " treeRows=" << workspace_tree_.visible().size()
      // The binding table's two focus areas, and the row it remembers. Reported
      // because "focused" and "remembered" are deliberately different states.
      << " kbArea=" << [this]() -> std::string {
           const core::KeybindingsSection *table = keybindings();
           // Same predicate the sections render with: while an overlay (a
           // prompt or the reset confirmation) owns the keyboard, no area of
           // the pane does.
           if (table == nullptr || state_.page != Page::Settings ||
               workspace_pane_ != WorkspacePane::TrackList ||
               overlayOwnsKeyboard())
             return "off";
           return std::string(table->focusArea());
         }()
      << " kbRow=" << [this] {
           const core::KeybindingsSection *table = keybindings();
           return table != nullptr ? table->cursorIndex() : -1;
         }()
      << " page=" << (state_.page == Page::Settings ? "core" : "vault")
      // The Core pane's cursor, by LABEL: a script can drive a setting by name
      // instead of counting rows that shift when a note is added.
      // Spaces become underscores: every state field is a whitespace token, so
      // a label like "Save and reconnect" must stay assertable.
      << " setting=" << [this] {
           std::string label = core_panel_ ? core_panel_->selectedSetting() : "";
           for (char &character : label) {
             if (character == ' ')
               character = '_';
           }
           return label;
         }()
      // The presentation mode is a separate axis from the section: a script has
      // to be able to tell "in the workspace" from "immersive is open".
      << " view="
      << (state_.presentation == PresentationMode::ImmersiveNowPlaying
              ? "immersive"
              : "normal")
      << " player=" << static_cast<int>(state_.player.state)
      // The connection contract, as the backend actually uses it: the socket
      // timeout must never be 0 (libmpdclient reads 0 as "wait forever") and
      // stop_on_exit decides whether quitting stops the daemon.
      // Whether MPD is actually connected, straight from the live state: the
      // release smoke tests need to tell "connected" from "defaults printed".
      << " mpd=" << (state_.mpd_connected ? 1 : 0)
      << " mpdTimeout=" << state_.settings.mpd_timeout_ms
      << " stopOnExit=" << (controller_.config().stop_on_exit ? 1 : 0)
      << " songId=" << state_.player.current_song_id
      << " uri=" << (state_.player.current_song ? state_.player.current_song->uri : "-")
      // Live immersive geometry, straight from the single metrics source: the
      // layout is reported, never measured from a rendered string.
      << " body=" << metrics_.immersive.body_width << "x"
      << metrics_.immersive.body_height
      << " padL=" << metrics_.immersive.outer_left_pad
      << " padR=" << metrics_.immersive.outer_right_pad
      << " content=" << metrics_.immersive.content_width
      << " infoRows=" << metrics_.immersive.info_rows
      << " infoGap=" << metrics_.immersive.info_gap_rows
      << " vizTopPad=" << metrics_.immersive.visualizer_top_pad
      << " vizBottomPad=" << metrics_.immersive.visualizer_bottom_pad
      << " vizRows=" << metrics_.immersive.visualizer_container_rows
      << " vizCols=" << metrics_.immersive.visualizer_container_columns
      // The drawable grid the Spectrum draws in: origin and drawn size, from
      // the metrics. `vizRows` / `gridRows` differ because the grid floats
      // inside its container, and the baseline is the centre of the GRID.
      << " gridLeft=" << metrics_.immersive.visualizer_grid_left
      << " gridTop=" << metrics_.immersive.visualizer_grid_top
      << " gridCols=" << metrics_.immersive.visualizer_grid_columns
      << " gridRows=" << metrics_.immersive.visualizer_grid_rows
      << " bands=" << metrics_.immersive.visualizer_bands
      << " levels=" << metrics_.immersive.visualizer_levels
      << " strideX=" << metrics_.immersive.visualizer_column_stride
      << " strideY=" << metrics_.immersive.visualizer_row_stride
      << " playerGap=" << metrics_.immersive.player_gap_rows
      // Signal level, so a test can tell "no data" from "no output".
      << " vizSignal=" << state_.visualizer.position.size() << ":"
      << [&] {
           float top = 0.0F;
           for (const float value : state_.visualizer.position)
             top = std::max(top, value);
           return static_cast<int>(std::lround(top * 100.0F));
         }()
      << " vizLive=" << (state_.visualizer.data_available ? 1 : 0)
      // The selected immersive visual. Spectrum keeps its detailed renderer
      // counters; Disc exposes its tonearm and geometry state below.
      << " viz="
      << (state_.display_mode == DisplayMode::Disc
              ? "disc"
              : (visualizer_ ? visualizer_->id()
                             : termusic::ui::kVisualizerId))
      << " vizPalette="
      << termusic::ui::normalizeVisualizerPaletteId(
             controller_.config().visualizer_palette)
      << " vizCells=" << (visualizer_ ? visualizer_->stats().drawn : 0)
      << " vizHeld=" << (visualizer_ ? visualizer_->stats().retained : 0)
      << " vizCap=" << (visualizer_ ? visualizer_->stats().capacity : 0)
      << " vizBase=" << (visualizer_ ? visualizer_->stats().baseline : 0)
      << " vizMain=" << (visualizer_ ? visualizer_->stats().main_rows : 0)
      << " vizBars=" << (visualizer_ ? visualizer_->stats().bars : 0)
      << " vizDots=" << (visualizer_ ? visualizer_->stats().dots : 0)
      << " discFit=" << (!disc_ || disc_->stats().geometry_fits ? 1 : 0)
      // The sidebar's two regions, from the metrics: a script can assert the
      // responsive behaviour without measuring pixels.
      << " sideRows=" << metrics_.sidebar.rows
      << " sideTree=" << metrics_.sidebar.tree_rows
      << " sideInfo=" << metrics_.sidebar.playback_rows
      << " sideGap=" << metrics_.sidebar.playback_top_gap
      << " sidePad=" << metrics_.sidebar.playback_bottom_pad
      << " sideW=" << metrics_.sidebar.width
      // What the block actually DREW: whether it is on screen, the role of its
      // accent bar, and the role of each line top-down. A text frame cannot
      // show colour or weight, so the hierarchy is reported rather than
      // guessed at from glyphs.
      << " sideShown=" << (sidebar_roles_.empty() ? 0 : 1)
      << " sideBar=" << (sidebar_bar_.empty() ? "none" : sidebar_bar_)
      << " sideRoles=" << [this] {
           std::string joined;
           for (const std::string &role : sidebar_roles_) {
             if (!joined.empty())
               joined += ",";
             joined += role;
           }
           return joined.empty() ? std::string("none") : joined;
         }()
      << " beats=" << beat_.beats
      << " beatBpm=" << static_cast<int>(std::lround(beat_.bpm))
      << " beatLast=" << static_cast<int>(std::lround(beat_.last_interval * 1000.0))
      << " beatMean=" << static_cast<int>(std::lround(beat_.mean_interval * 1000.0))
      << " beatEnv=" << static_cast<int>(std::lround(beat_.envelope * 100.0))
      // What the onset test actually compares: low-band energy against its
      // rolling reference. Without these two, "no beats" is unreadable.
      << " beatLow=" << static_cast<int>(std::lround(low_energy_ * 100.0))
      << " beatBase=" << static_cast<int>(std::lround(beat_.baseline * 100.0))
      << " vizBand=" << [&] {
           int best = -1;
           float top = -1.0F;
           for (std::size_t i = 0; i < state_.visualizer.position.size(); ++i)
             if (state_.visualizer.position[i] > top) {
               top = state_.visualizer.position[i];
               best = static_cast<int>(i);
             }
           return best;
         }()
      << " random=" << (state_.player.random ? 1 : 0)
      // Volume and repeat are LOAD-BEARING for the shortcut cleanup: a removed
      // `+`/`-` must leave the volume alone, and `r` must still toggle repeat.
      // Reporting them is what lets a script assert that instead of assuming.
      << " repeat=" << (state_.player.repeat ? 1 : 0)
      << " volume=" << state_.player.volume
      // The Player Bar's own budget: one playback-mode control (Shuffle), so
      // the cluster is four buttons wide and the progress track gets the rest.
      << " controlsWidth=" << metrics_.controls_width
      << " progressWidth=" << metrics_.progress_width;
  return out.str();
}

Element Application::renderWorkspace() {
  // The block's report describes THIS frame: clearing it here means a frame
  // that does not draw the sidebar (single-pane track list) reports nothing.
  sidebar_roles_.clear();
  sidebar_bar_.clear();
  // ONE frame, ONE vertical divider. The panes contribute content only, so
  // there is no gutter, no double border and no gap at the join.
  //
  // The Sidebar is TWO regions and the space between them: the tree is
  // top-anchored, the playback block is bottom-anchored, and the filler takes
  // whatever is left. Nothing here counts blank rows -- the layout does it.
  Element sidebar = vbox({
      renderTreePane(),
      filler(),
      renderSidebarPlayback(),
  });
  // Under `core` the right pane belongs to the selected entry's module; under
  // `vault` it is the Track Buffer for the selected collection.
  Element body;
  if (state_.page == Page::Settings && core_panel_) {
    // The Tree is the only navigation, so the panel always follows it: one
    // selection, one pane, no second menu to keep in step.
    if (const int section = coreSectionIndex(); section >= 0)
      core_panel_->select(section);
    body = core_panel_->render(*core_context_);
  } else {
    body = renderTrackBuffer();
  }
  if (metrics_.workspace_single_pane) {
    // Narrow terminals show one pane at a time, but the frame and the
    // divider stay identical so the layout never changes shape.
    return panel(workspace_pane_ == WorkspacePane::Tree ? std::move(sidebar)
                                                        : std::move(body));
  }
  return panel(hbox({
      std::move(sidebar) |
          size(WIDTH, EQUAL, std::max(12, metrics_.workspace_tree_width)),
      separator(),
      std::move(body) | reflect(track_area_box_) | flex,
  }));
}

bool Application::bottomBoxVisible() {
  // The bottom row is EMPTY unless it is carrying an interaction. There is no
  // permanent hint line, no key legend and no status text any more; feedback
  // travels through the transient toast instead.
  core::KeybindingsSection *table = keybindings();
  return search_prompt_ || playlist_prompt_ || delete_playlist_pending_ ||
         (table != nullptr && table->resetPending());
}

Element Application::inputBox(const std::string &label,
                              const std::string &value, bool alert) const {
  // ONE input look for the whole application: a white frame, a short label and
  // the caret. No key legend -- the box is a control, not a manual.
  const Color frame = alert ? theme_.error : theme_.input_border;
  const Color accent = alert ? theme_.error : theme_.accent_secondary;
  return vbox(Elements{
             hbox(Elements{
                 text(" " + label + " ") | bold | color(accent),
                 text(value) | color(theme_.text),
                 text("\u2588") | bold | color(frame),
                 filler(),
             }),
         }) |
         borderStyled(ROUNDED, frame);
}

Element Application::confirmBox(const std::string &question) const {
  // A destructive question, framed like every other input. The keys are the
  // conventional ones, and naming them here is exactly the permanent hint text
  // this round removes.
  return vbox(Elements{
             hbox(Elements{text(" " + question) | bold | color(theme_.text),
                           filler()}),
         }) |
         borderStyled(ROUNDED, theme_.input_border);
}

Element Application::renderBottomBox() {
  core::KeybindingsSection *table = keybindings();
  // A pending confirmation owns the row while it waits: nothing else may look
  // like the thing that is being asked about.
  if (table != nullptr && table->resetPending())
    return confirmBox("Restore default keybindings?");
  if (delete_playlist_pending_) {
    const int index = treePlaylistIndex();
    const std::string name =
        index >= 0 ? state_.library.playlists[static_cast<std::size_t>(index)]
                   : std::string();
    return confirmBox("Delete playlist \"" + name + "\"?");
  }
  if (search_prompt_)
    return renderSearchInput();
  if (playlist_prompt_)
    return inputBox(playlist_prompt_rename_ ? "rename" : "new playlist",
                    playlist_prompt_text_, false);
  return text("");
}

Element Application::renderSearchInput() {
  // The box keeps the shape the search line always had -- a framed row, a short
  // label, the caret -- but the value and the caret now come from the
  // persistent FTXUI Input, so what is typed is edited as UTF-8 and the
  // terminal cursor is placed on the caret, where an IME needs it.
  using namespace ftxui;
  const Color frame = search_no_match_ ? theme_.error : theme_.input_border;
  const Color accent =
      search_no_match_ ? theme_.error : theme_.accent_secondary;
  return vbox(Elements{
             hbox(Elements{
                 text(" / ") | bold | color(accent),
                 search_input_->Render() | flex,
             }),
         }) |
         borderStyled(ROUNDED, frame);
}

bool Application::toastVisible() const {
  const auto now = std::chrono::steady_clock::now();
  if (!visual_message_.empty() &&
      now - toast_at_ <= std::chrono::milliseconds(kToastMs))
    return true;
  return state_.toast.has_value() &&
         now - state_.toast_at <= std::chrono::milliseconds(kToastMs);
}

Element Application::renderToast() {
  // Bottom right, one line, self-expiring. This is the lightweight replacement
  // for the old permanent status bar: it never reserves a row of its own and
  // it never accumulates text.
  std::string message = visual_message_;
  const bool error = visual_message_error_;
  if (message.empty())
    message = state_.toast.value_or(std::string());
  if (message.empty())
    return text("");
  return vbox(Elements{
      filler(),
      hbox(Elements{
          filler(),
          text(" " + message + " ") | bgcolor(theme_.panel) |
              color(error ? theme_.error : theme_.text),
      }),
      // One clear row below it, so the toast never sits ON the frame border.
      text(""),
  });
}

void Application::syncVisualizerSpectrum() {
  if (state_.demo)
    return;
  // The analyzer only analyses while playback is active, and this is the one
  // place that tells it so: driving it from the timer path means the spectrum
  // works with or without a screen loop, instead of depending on a repaint
  // callback being delivered first.
  analyzer_.setPlaybackActive(state_.player.state == PlaybackState::Playing);
  SpectrumSnapshot spectrum = analyzer_.snapshot();
  state_.visualizer.bars = std::move(spectrum.bars);
  state_.visualizer.peaks = std::move(spectrum.peaks);
  state_.visualizer.data_available = spectrum.data_available;
}

void Application::ensureVisualizerRenderer() {
  // ONE renderer, created once. There is no style to look up and no factory
  // switch to walk: the Spectrum is the visualizer, and the palette it draws in
  // arrives with every frame (see visualizerFrame), so a palette change needs
  // no rebuild at all.
  if (visualizer_ != nullptr)
    return;
  visualizer_ = ui::makeSpectrumRenderer();
  visualizer_->reset();
}

double Application::lowBandOnset() const {
  // What the detector wants is an ONSET, not a level: the low bands of a
  // compressed mix sit near full scale even between hits, so the signal is the
  // FAST envelope rising above the SLOW one. Shared by every style.
  const auto &fast = state_.visualizer.fast_envelope;
  const auto &slow = state_.visualizer.slow_envelope;
  const int band_total = static_cast<int>(state_.visualizer.position.size());
  const int low_bands = std::max(2, band_total / 4);
  const int pairs = std::min({static_cast<int>(fast.size()),
                              static_cast<int>(slow.size()), low_bands});
  if (pairs <= 0)
    return 0.0;
  double sum = 0.0;
  for (int band = 0; band < pairs; ++band)
    sum += std::max(0.0F, fast[static_cast<std::size_t>(band)] -
                               slow[static_cast<std::size_t>(band)]);
  return sum / static_cast<double>(pairs);
}

ui::VisualizerFrame Application::visualizerFrame(
    const ui::VisualizerPalette &palette, double dt) {
  const ImmersiveLayout &layout = metrics_.immersive;
  const bool live = state_.visualizer.data_available || motion_test_;
  return ui::VisualizerFrame{
      .bands = state_.visualizer.position,
      // The Spectrum draws across the CONTAINER, not the old inset grid: the
      // full drawable width, with its own small margin inside it.
      .columns = std::max(0, layout.visualizer_container_columns),
      .rows = std::max(0, layout.visualizer_container_rows),
      .beat = static_cast<float>(beat_.envelope),
      .dt = static_cast<float>(std::clamp(dt, 1.0 / 240.0, 0.05)),
      .live = live,
      .theme = theme_,
      .palette = palette,
  };
}

Element Application::renderImmersiveVisualizer() {
  const auto now = std::chrono::steady_clock::now();
  double dt = 1.0 / 60.0;
  if (last_visualizer_time_.time_since_epoch().count() != 0)
    dt = std::chrono::duration<double>(now - last_visualizer_time_).count();
  last_visualizer_time_ = now;

  if (state_.display_mode == DisplayMode::Disc) {
    if (!disc_)
      disc_ = std::make_unique<ui::DiscRenderer>();
    const ImmersiveLayout &layout = metrics_.immersive;
    ui::DiscFrame frame{
        .columns = std::max(0, layout.visualizer_container_columns),
        .rows = std::max(0, layout.visualizer_container_rows),
        .dt = std::clamp(dt, 1.0 / 240.0, 0.12),
        .playback = state_.player.state,
    };
    disc_->update(frame);
    return disc_->render(frame);
  }

  // Spectrum keeps its existing data, beat and rendering path unchanged.
  ensureVisualizerRenderer();
  dt = std::clamp(dt, 1.0 / 240.0, 0.05);
  low_energy_ = lowBandOnset();
  beat_ = beatStep(beat_, low_energy_, dt);

  // The palette is read from the configuration on every frame: it is the one
  // visualizer setting that changes what the cells look like, and nothing has
  // to be rebuilt for it.
  const ui::VisualizerPalette &palette = ui::visualizerPalette(
      controller_.config().visualizer_palette);
  ui::VisualizerFrame frame = visualizerFrame(palette, dt);
  visualizer_->update(frame);
  return visualizer_->render(frame);
}

namespace {

/// Vertically centres a one-row element inside a taller content area. Using
/// fillers is deterministic; `| vcenter` is not honoured once a `| size`
/// decorator is applied to the same element.
ftxui::Element midRow(ftxui::Element element) {
  using namespace ftxui;
  return vbox({filler(), std::move(element), filler()});
}

} // namespace

Element Application::renderTransportButton(Icon icon, bool focused,
                                            bool highlighted, int box_height,
                                            bool hovered, bool toggle) {
  const int box_width = metrics_.playback_button_width;
  return transportButton(icon, icon_set_, theme_, focused, highlighted,
                         box_width, box_height, hovered, toggle);
}

Element Application::squareProgress(const SliderGeometry &geometry,
                                    const Color &filled_color,
                                    const Color &empty_color) const {
  // One square per terminal cell. The fill COUNT comes from the continuous
  // progress so the endpoints are exact: 0% -> no filled square, 100% -> no
  // empty square. SliderGeometry itself is untouched.
  const int cells = std::max(1, geometry.track_cells);
  const int filled = std::clamp(
      static_cast<int>(std::lround(geometry.progress * cells)), 0, cells);
  Elements out;
  out.reserve(static_cast<std::size_t>(cells));
  for (int index = 0; index < cells; ++index) {
    const bool is_filled = index < filled;
    out.push_back(text(is_filled ? "\u25aa" : "\u25ab") |
                   color(is_filled ? filled_color : empty_color));
  }
  return hbox(std::move(out));
}

Element Application::renderPlayerBar() {
  const auto now = std::chrono::steady_clock::now();

  // UI-only synthetic track for slider evaluation: a fixed 255 s duration whose
  // elapsed advances from steady_clock. It never reads or writes MPD.
  double ui_elapsed = 0.0;
  double ui_progress = 0.0;
  const bool ui_test = ui_slider_test_;
  if (ui_test) {
    constexpr double kTestDuration = 255.0;
    const double seconds =
        std::chrono::duration<double>(now - ui_test_start_).count();
    ui_elapsed = std::fmod(seconds, kTestDuration);
    ui_progress = ui_elapsed / kTestDuration;
  }

  const std::string elapsed =
      util::formatDuration(ui_test ? ui_elapsed : displayElapsed(state_, now));
  const std::string total =
      ui_test ? util::formatDuration(255.0)
              : (state_.player.duration_seconds > 0.0
                     ? util::formatDuration(state_.player.duration_seconds)
                     : "--:--");

  // The control row has no panel around it any more: the glyphs get the
  // rows they need directly.
  const int box_height = std::max(1, metrics_.control_height);
  const bool player_focused = state_.focus == FocusArea::PlayerBar;

  // Fixed-width, centred time labels so the track never shifts when the digits
  // change.
  const Element elapsed_label =
      midRow(text(util::padLeft(elapsed, 5)) | color(theme_.header_text));
  const Element total_label =
      midRow(text(util::padRight(total, 5)) | color(theme_.weak_text));

  const double shown_progress =
      drag_progress_ >= 0.0
          ? drag_progress_
          : (ui_test ? ui_progress : displayProgress(state_, now));
  const SliderGeometry progress_geometry =
      SliderGeometry::fromProgress(metrics_.progress_width, shown_progress);
  const Element track =
      midRow(squareProgress(progress_geometry, theme_.progress_filled,
                            theme_.progress_empty) |
             reflect(progress_box_));

  const int volume_width = std::max(0, metrics_.volume_width);
  const Element volume_track =
      metrics_.show_volume && volume_width > 0
          ? midRow(squareProgress(
                       SliderGeometry::fromProgress(
                           volume_width,
                           drag_volume_ >= 0.0
                               ? drag_volume_
                               : (state_.player.volume < 0
                                      ? 0.0
                                      : std::clamp(
                                            state_.player.volume / 100.0, 0.0,
                                            1.0))),
                       theme_.volume_fill, theme_.volume_empty) |
                   reflect(volume_box_))
          : text("");

  const Element gap2 =
      midRow(text(util::repeat(metrics_.player_gap_small, " ")));
  const Element gap3 =
      midRow(text(util::repeat(metrics_.player_gap_large, " ")));
  // A single divider between the timeline and the volume group, with clear
  // space on both sides -- never glued to the speaker.
  const Element divider =
      midRow(text("\u2502") | color(theme_.divider));
  const Element pad = midRow(text(util::repeat(metrics_.player_padding, " ")));
  const Element speaker =
      midRow(renderTransportButton(Icon::Speaker, false, false, 1));

  // The five transport buttons form one fixed-size cluster: explicit gaps, no
  // fillers, so widening the window never pulls them apart.
  Elements controls;
  const auto add_control = [&](Icon icon, bool focused, bool highlighted,
                               Box &box, int control_id, bool toggle = false) {
    if (!controls.empty())
      controls.push_back(text(util::repeat(metrics_.playback_button_gap, " ")));
    // Hover and press are presentation only: they never change geometry.
    const bool hovered = hover_control_ == control_id;
    const bool pressed = pressed_control_ == control_id;
    Element button =
        renderTransportButton(icon, focused, highlighted || pressed,
                              box_height, hovered, toggle);
    Element laid_out =
        box_height >= 3 ? std::move(button) : midRow(std::move(button));
    // reflect() records the box the layout engine actually assigned, so the
    // hit test can never disagree with what was drawn.
    controls.push_back(std::move(laid_out) | reflect(box));
  };
  // ONE playback-mode control. Sequential playback is simply "shuffle off":
  // the icon is always visible and its highlight is the MPD `random` flag, so
  // there is no second mode icon and no separate "SEQ" state to draw.
  if (metrics_.show_shuffle)
    add_control(Icon::Shuffle, false, state_.player.random, shuffle_box_, 0,
                true);
  add_control(Icon::Previous, false, false, previous_box_, 1);
  // Playback state is NOT keyboard focus: playing never draws the ring, and
  // the icon keeps one colour so Playing and Paused differ only by shape.
  add_control(state_.player.state == PlaybackState::Playing ? Icon::Pause
                                                            : Icon::Play,
              player_focused, false, play_box_, 2);
  add_control(Icon::Next, false, false, next_box_, 3);
  // Repeat is deliberately NOT part of the Player Bar any more. The action
  // still exists (global `r`), it is simply not one of the two playback-mode
  // controls here -- and no spacer is left behind: the cells it used to occupy
  // were never reserved, so the progress track absorbs them.

  Element transport =
      hbox(std::move(controls)) |
      size(WIDTH, EQUAL, std::max(12, metrics_.controls_width)) | vcenter;

  // --- Two-row fallback -----------------------------------------------------
  if (metrics_.player_two_rows) {
    Elements timeline = {elapsed_label, gap2, track, gap2, total_label};
    if (metrics_.show_volume && volume_width > 0) {
      timeline.push_back(gap3);
      timeline.push_back(divider);
      timeline.push_back(gap3);
      timeline.push_back(speaker);
      timeline.push_back(gap2);
      timeline.push_back(volume_track);
      timeline.push_back(gap2);
      timeline.push_back(midRow(
          text(util::padRight(util::formatVolume(state_.player.volume), 4)) |
          color(theme_.weak_text)));
    }
    return vbox({
        hbox({transport}) | hcenter,
        hbox(std::move(timeline)) | hcenter,
    });
  }

  // --- Single row: fixed cluster, flexible track ---------------------------
  Elements row = {pad, std::move(transport), gap2, elapsed_label, gap2, track,
                  gap2, total_label};
  if (metrics_.show_volume && volume_width > 0) {
    row.push_back(gap3);
    row.push_back(divider);
    row.push_back(gap3);
    row.push_back(speaker);
    row.push_back(gap2);
    row.push_back(volume_track);
    row.push_back(gap2);
    row.push_back(midRow(text(util::padRight(
                             util::formatVolume(state_.player.volume), 4)) |
                         color(theme_.weak_text)));
  }
  row.push_back(pad);
  return hbox(std::move(row)) | vcenter;
}


Element Application::renderModal() {
  Element content;
  switch (modal_) {
  case Modal::NewPlaylist:
    content = vbox(
        {text("New playlist") | bold,
         hbox({text("Name: "), modal_input_->Render() | flex}),
         text("Enter to create \u00b7 Esc to cancel") | color(theme_.muted_text)});
    break;
  case Modal::RenamePlaylist:
    content = vbox(
        {text("Rename playlist") | bold,
         hbox({text("Name: "), modal_input_->Render() | flex}),
         text("Enter to rename \u00b7 Esc to cancel") | color(theme_.muted_text)});
    break;
  case Modal::DeletePlaylist:
    content = vbox(
        {text("Delete this playlist?") | bold | color(theme_.error),
         text("This cannot be undone."),
         text("Enter to delete \u00b7 Esc to cancel") | color(theme_.muted_text)});
    break;
  case Modal::ChoosePlaylist:
    content = vbox(
        {text("Add song to playlist") | bold,
         playlist_target_menu_->Render() | frame | size(HEIGHT, LESS_THAN, 8),
         text("Enter to add \u00b7 Esc to cancel") | color(theme_.muted_text)});
    break;
  case Modal::None:
    return text("");
  }
  return content | size(WIDTH, EQUAL, 46) |
         borderStyled(ROUNDED, theme_.border) | bgcolor(theme_.background);
}

Element Application::renderHelpOverlay() const {
  Elements rows = {
      text("termusic — 快捷键") | bold | color(theme_.accent_primary),
      text(""),
  };
  const auto line = [this](std::string keys, std::string label) {
    return hbox({text(util::padRight(keys, 16)) | bold |
                     color(theme_.accent_primary),
                 text(label) | color(theme_.text)});
  };
  const std::vector<std::pair<std::string, std::string>> entries = {
      {"1 / 2", "切换 Library / Settings"},
      {"j / k  ↑ / ↓", "上下选择"},
      {"gg / G", "跳到首行 / 末行"},
      {"Enter", "播放或暂停"},
      {"a", "添加到队列"},
      {"d", "从队列移除"},
      {"c", "清空队列"},
      {"h / l  ← / →", "上一级目录 / 进入目录，播放条上为快退 / 快进"},
      {"[ / ]", "切换播放列表"},
      {"n / p", "下一首 / 上一首"},
      {"+ / -", "音量增减"},
      {"s / r", "随机 / 循环"},
      {"/", "搜索"},
      {"< > ?", "显示或隐藏本帮助"},
      {"R", "重新连接 MPD"},
      {"q", "退出"},
  };
  for (const auto &entry : entries)
    rows.push_back(line(entry.first, entry.second));
  return vbox(std::move(rows)) | size(WIDTH, EQUAL, 72) |
         borderStyled(ROUNDED, theme_.border) | bgcolor(theme_.background);
}


core::SettingsSection *Application::selectedCoreSection() {
  // The section the tree cursor points at. The panel is the authority for the
  // index; the tree only names the entry.
  if (core_panel_ == nullptr || state_.page != Page::Settings)
    return nullptr;
  const TreeNode *node = workspace_tree_.current();
  if (node == nullptr || node->type != TreeNodeType::CoreSection)
    return nullptr;
  for (const ConfigFileEntry &entry : kCoreSections) {
    if (entry.id == node->id)
      return core_panel_->section(entry.section);
  }
  return nullptr;
}

core::KeybindingsSection *Application::keybindings() {
  return core_panel_ != nullptr ? core_panel_->keybindings() : nullptr;
}

const core::KeybindingsSection *Application::keybindings() const {
  return core_panel_ != nullptr ? core_panel_->keybindings() : nullptr;
}

bool Application::coreEditing() const {
  // The section knows whether one of ITS text fields is focused. Asking the
  // component tree instead would have to guess, and FTXUI gives a pane's first
  // focusable widget the focus as soon as the pane is shown -- so "something is
  // focused" is not the same as "the user is typing".
  return core_panel_ != nullptr && core_panel_->editing();
}

bool Application::capturingBinding() const {
  const auto *table =
      core_panel_ != nullptr ? core_panel_->keybindings() : nullptr;
  return table != nullptr && table->capturing();
}

bool Application::handleCaptureKey(const Event &event) {
  core::KeybindingsSection *table = keybindings();
  if (table == nullptr || !table->capturing())
    return false;
  // Esc is reserved for cancelling the capture, so Esc cannot itself be bound
  // here. Documented limitation; no fragile escape-escaping scheme.
  if (event == Event::Escape) {
    table->cancelCapture();
    visual_message_ = "Capture cancelled";
    screen_.PostEvent(Event::Custom);
    return true;
  }
  if (event.is_mouse())
    return true;
  // Same normalization as runtime dispatch, so a captured "Ctrl+l" is the same
  // token the Keymap will later match.
  table->appendCaptureToken(normalizeKey(event, false));
  screen_.PostEvent(Event::Custom);
  return true;
}

void Application::syncKeymapMessage() {
  if (core::KeybindingsSection *table = keybindings(); table != nullptr) {
    // The table owns the wording; the workspace shows the same sentence in its
    // one status row, so the two can never disagree.
    visual_message_ = table->takeMessage();
    screen_.PostEvent(Event::Custom);
  }
}

bool Application::moveWorkspaceCursor(int delta) {
  // The tree is the navigation for BOTH directories, and `j`/`k` are the keys
  // that walk it. Every landing loads the node it lands on, so the pane can
  // never disagree with the tree about what is open. The keyboard is NOT moved
  // by this: only Enter does that.
  if (delta == 0)
    return true;
  if (workspace_pane_ == WorkspacePane::Tree) {
    workspace_tree_.moveCursor(delta);
    autoLoadTreeCursor();
    return true;
  }
  if (state_.page == Page::Settings) {
    // The pane is the editor once it owns the keyboard, so `j`/`k` move ITS
    // cursor -- the same rule for every settings module, which is what makes
    // "Enter, then keep walking" feel like one list everywhere. The binding
    // table has its own cursor implementation; a text field owns the keyboard
    // while it is being typed into.
    if (keybindingsOpen()) {
      if (core::KeybindingsSection *table = keybindings())
        table->moveCursor(delta);
      return true;
    }
    if (coreEditing())
      return false; // the field wants the key; Esc leaves it
    if (core::SettingsSection *section = selectedCoreSection()) {
      section->moveCursor(delta);
      return true;
    }
    return false;
  }
  // Track list wraps in Normal mode: last + j -> first, first + k -> last.
  // Visual mode clamps instead, because a wrapped range that spans both ends of
  // the buffer is not a selection.
  const int count = static_cast<int>(activeTracks().size());
  if (count <= 0)
    return true;
  if (visualActive()) {
    track_cursor_ = std::clamp(track_cursor_ + delta, 0, count - 1);
    return true;
  }
  track_cursor_ = ((track_cursor_ + delta) % count + count) % count;
  // Keep the viewport coherent with the wrapped cursor.
  if (delta > 0 && track_cursor_ == 0)
    track_scroll_ = 0;
  else if (delta < 0 && track_cursor_ == count - 1)
    track_scroll_ = count; // clamped against the real size during render
  return true;
}

bool Application::handleEvent(Event event) {
  if (event == Event::Custom) {
    processTimers();
    return true;
  }
  if (modal_ != Modal::None)
    return handleModalEvent(event);

  if (capturingBinding() && handleCaptureKey(event))
    return true;

  // Quit is global and must escape every state that is not text entry. The
  // guards below deliberately swallow keys to protect a focused widget (the
  // Core text inputs, the Files search box) or a pending confirmation; without
  // this check a focused field could trap the user inside the application.
  // Text entry itself keeps priority, so `q` still types a `q` in a playlist
  // name, a modal field or a keybinding capture.
  if (!textEntryActive() && !capturingBinding() && modal_ == Modal::None &&
      isQuitKey(event)) {
    requestQuit();
    return true;
  }

  if (state_.page == Page::Settings && coreEditing()) {
    // A `core` field owns its keystrokes. Esc leaves the FIELD first and only
    // then hands the keyboard back to the tree, so one key never skips a step;
    // no field may trap the user in a pane either way.
    if (event == Event::Escape) {
      core::SettingsSection *section = selectedCoreSection();
      if (section != nullptr && section->leaveEditing()) {
        screen_.PostEvent(Event::Custom);
        return true;
      }
      workspace_pane_ = WorkspacePane::Tree;
      focusCurrentList();
      return true;
    }
    core::SettingsSection *section = selectedCoreSection();
    if (section != nullptr)
      return section->inputKey(event);
    return false;
  }

  if (state_.page == Page::Library) {
    if (event.is_mouse() && handleWorkspaceMouse(event.mouse()))
      return true;
    if (!event.is_mouse() && handleWorkspaceKey(event))
      return true;
  }

  if (event.is_mouse()) {
    const auto &mouse = event.mouse();


    // ---- Transport controls ------------------------------------------------
    // Handled here rather than by the FTXUI Button nodes so that a click works
    // regardless of keyboard focus, and so the action fires exactly once (on
    // release, never on press as well).
    if (mouse.button == Mouse::Left || mouse.button == Mouse::None) {
      const std::pair<const Box *, int> targets[] = {
          {&shuffle_box_, 0}, {&previous_box_, 1}, {&play_box_, 2},
          {&next_box_, 3},
      };
      int hit = -1;
      for (const auto &target : targets) {
        if (!target.first->IsEmpty() &&
            target.first->Contain(mouse.x, mouse.y)) {
          hit = target.second;
          break;
        }
      }
      if (mouse.motion == Mouse::Moved) {
        hover_control_ = hit;
        if (mouse_debug_)
          mouse_debug_text_ =
              "move " + std::to_string(mouse.x) + "," +
              std::to_string(mouse.y) + " hit=" +
              (hit < 0 ? std::string("none") : "control" + std::to_string(hit));
        return false; // let hover fall through to the rest of the UI
      }
      if (mouse.motion == Mouse::Pressed && hit >= 0) {
        pressed_control_ = hit;
        mouse_debug_text_ = "press control" + std::to_string(hit);
        screen_.PostEvent(Event::Custom);
        return true;
      }
      if (mouse.motion == Mouse::Released) {
        const int pressed = pressed_control_;
        pressed_control_ = -1;
        if (pressed >= 0 && pressed == hit) {
          switch (pressed) {
          case 0:
            dispatch(Action::ToggleShuffle);
            break;
          case 1:
            dispatch(Action::Previous);
            break;
          case 2:
            dispatch(Action::TogglePlay);
            break;
          case 3:
            dispatch(Action::Next);
            break;
          default:
            break;
          }
          mouse_debug_text_ = "action control" + std::to_string(pressed);
          screen_.PostEvent(Event::Custom);
          return true;
        }
        if (pressed >= 0) {
          screen_.PostEvent(Event::Custom);
          return true;
        }
      }
    }

    const bool on_progress = progress_box_.Contain(mouse.x, mouse.y);
    const bool on_volume = volume_box_.Contain(mouse.x, mouse.y);

    // Map an absolute column to 0..1 across a slider track.
    // Same model as the renderer: a cell offset maps back through
    // SliderGeometry, so hit-testing and drawing can never disagree.
    const auto ratio_at = [](const Box &box, int x) {
      const int cells = std::max(1, box.x_max - box.x_min + 1);
      const double offset =
          static_cast<double>(std::clamp(x - box.x_min, 0, cells - 1));
      return SliderGeometry::progressFromPosition(cells, offset);
    };

    if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
      if (on_progress)
        drag_target_ = 1;
      else if (on_volume)
        drag_target_ = 2;
      if (drag_target_ != 0 && mouse_debug_)
        mouse_debug_text_ = drag_target_ == 1 ? "drag progress begin"
                                              : "drag volume begin";
      if (drag_target_ != 0) {
        screen_.PostEvent(Event::Custom);
        // fall through: the preview below runs on this same event
      }
    }

    // While dragging, only the local preview moves: the thumb tracks the
    // pointer one-to-one without quantisation, and MPD is not touched.
    if (drag_target_ == 1) {
      drag_progress_ = ratio_at(progress_box_, mouse.x);
      if (mouse_debug_)
        mouse_debug_text_ = "progress prev=" +
                            std::to_string(drag_progress_).substr(0, 5);
      screen_.PostEvent(Event::Custom);
      if (mouse.motion == Mouse::Released) {
        if (state_.player.duration_seconds > 0.0)
          controller_.seekAbsolute(drag_progress_ *
                                   state_.player.duration_seconds);
        drag_target_ = 0;
        drag_progress_ = -1.0;
      }
      return true;
    }
    if (drag_target_ == 2) {
      drag_volume_ = ratio_at(volume_box_, mouse.x);
      if (mouse_debug_)
        mouse_debug_text_ =
            "volume prev=" + std::to_string(drag_volume_).substr(0, 5);
      screen_.PostEvent(Event::Custom);
      if (mouse.motion == Mouse::Released) {
        controller_.setVolume(
            static_cast<int>(std::lround(drag_volume_ * 100.0)));
        drag_target_ = 0;
        drag_volume_ = -1.0;
      }
      return true;
    }
    if (mouse.motion == Mouse::Released)
      drag_target_ = 0;

    return false;
  }

  // Nothing else claims the key. There is deliberately no fallback to a
  // physical-key table here: an unresolved configured key stays unresolved,
  // which is what makes user overrides actually replace the defaults.
  return false;
}

bool Application::handleModalEvent(Event event) {
  if (event == Event::Escape) {
    closeModal();
    return true;
  }
  if (event == Event::Return) {
    confirmModal();
    return true;
  }
  if (modal_ == Modal::ChoosePlaylist) {
    (void)playlist_target_menu_->OnEvent(event);
  } else if (modal_ == Modal::NewPlaylist || modal_ == Modal::RenamePlaylist) {
    (void)modal_input_->OnEvent(event);
  }
  return true;
}

void Application::updateVisualizerMotion() {
  constexpr int kCanonicalBands = 96;
  constexpr float kRiseStiffness = 58.0F;
  constexpr float kRiseDamping = 11.5F;
  constexpr float kFallStiffness = 26.0F;
  constexpr float kFallDamping = 9.0F;

  const auto now = std::chrono::steady_clock::now();
  double dt = 0.0;
  if (last_motion_time_.time_since_epoch().count() != 0)
    dt = std::chrono::duration<double>(now - last_motion_time_).count();
  last_motion_time_ = now;
  dt = std::clamp(dt, 1.0 / 240.0, 0.05);
  const auto step = static_cast<float>(dt);

  const bool live = state_.visualizer.data_available;
  std::vector<float> fallback;
  if (!live && motion_test_) {
    const double seconds =
        std::chrono::duration<double>(now.time_since_epoch()).count();
    fallback = fallbackSpectrum(kCanonicalBands, seconds);
  }
  const std::vector<float> &source = live ? state_.visualizer.bars : fallback;

  auto &position = state_.visualizer.position;
  auto &velocity = state_.visualizer.velocity;
  if (position.size() != kCanonicalBands) {
    // Resample on resize instead of resetting, so dragging a window keeps the
    // motion alive.
    std::vector<float> next_position(kCanonicalBands, 0.0F);
    std::vector<float> next_velocity(kCanonicalBands, 0.0F);
    for (int band = 0; band < kCanonicalBands; ++band) {
      if (position.empty())
        continue;
      const auto at = static_cast<std::size_t>(
          std::min(position.size() - 1,
                   static_cast<std::size_t>(
                       static_cast<double>(band) /
                       static_cast<double>(kCanonicalBands - 1) *
                       static_cast<double>(position.size() - 1))));
      next_position[static_cast<std::size_t>(band)] = position[at];
      next_velocity[static_cast<std::size_t>(band)] = velocity[at];
    }
    position = std::move(next_position);
    velocity = std::move(next_velocity);
  }

  std::vector<float> target(static_cast<std::size_t>(kCanonicalBands), 0.0F);

  // --- Two-timescale envelopes, all dt-based -------------------------------
  // alpha = 1 - exp(-dt/tau) keeps the response identical whether the analyzer
  // runs at 60 Hz or 30 Hz.
  const auto alpha = [step](double tau_seconds) {
    return static_cast<float>(1.0 - std::exp(-static_cast<double>(step) /
                                             tau_seconds));
  };
  constexpr double kFastAttack = 0.035;   // 35 ms
  constexpr double kFastRelease = 0.145;  // 145 ms
  constexpr double kSlowAttack = 0.230;   // 230 ms
  constexpr double kSlowRelease = 0.600;  // 600 ms
  constexpr double kReflectionTau = 0.100; // 100 ms
  const float fast_rise = alpha(kFastAttack);
  const float fast_fall = alpha(kFastRelease);
  const float slow_rise = alpha(kSlowAttack);
  const float slow_fall = alpha(kSlowRelease);
  const float reflection_alpha = alpha(kReflectionTau);

  auto &fast = state_.visualizer.fast_envelope;
  auto &slow = state_.visualizer.slow_envelope;
  auto &mirror_state = state_.visualizer.reflection;
  if (fast.size() != kCanonicalBands) {
    fast.assign(kCanonicalBands, 0.0F);
    slow.assign(kCanonicalBands, 0.0F);
    mirror_state.assign(kCanonicalBands, 0.0F);
  }

  std::vector<float> kernelled(static_cast<std::size_t>(kCanonicalBands), 0.0F);
  for (int band = 0; band < kCanonicalBands; ++band) {
    const auto raw_at = [&](int index) {
      if (source.empty())
        return 0.0F;
      const auto at = static_cast<std::size_t>(
          std::min(source.size() - 1,
                   static_cast<std::size_t>(
                       static_cast<double>(std::clamp(index, 0,
                                                      kCanonicalBands - 1)) /
                       static_cast<double>(kCanonicalBands - 1) *
                       static_cast<double>(source.size() - 1))));
      return std::clamp(source[at], 0.0F, 1.0F);
    };
    kernelled[static_cast<std::size_t>(band)] =
        0.15F * raw_at(band - 1) + 0.70F * raw_at(band) +
        0.15F * raw_at(band + 1);
  }

  // --- Visual Headroom Controller (presentation only) ----------------------
  // A fixed gain cannot serve both quiet and loud material: loud broadband
  // content piles every band against the ceiling and the peaks/valleys vanish.
  // Track the frame's P95 and drive it toward a target, with a fast reduction
  // (so a sudden loud passage never sits saturated) and a slow recovery.
  const auto frame_p95 = [&kernelled]() {
    std::vector<float> sorted(kernelled);
    const auto at = static_cast<std::size_t>(
        0.95 * static_cast<double>(sorted.size() - 1));
    std::nth_element(sorted.begin(),
                     sorted.begin() + static_cast<std::ptrdiff_t>(at),
                     sorted.end());
    return sorted[at];
  }();
  constexpr float kTargetP95 = 0.86F;
  constexpr float kGainMin = 0.65F;
  constexpr float kGainMax = 1.35F;
  constexpr double kGainAttack = 0.070;  // 70 ms  : clamp down quickly
  constexpr double kGainRelease = 1.200; // 1200 ms: recover gently
  const float desired_gain =
      std::clamp(kTargetP95 / std::max(frame_p95, 0.02F), kGainMin, kGainMax);
  const double gain_tau =
      desired_gain < headroom_gain_ ? kGainAttack : kGainRelease;
  headroom_gain_ += (desired_gain - headroom_gain_) *
                    static_cast<float>(1.0 - std::exp(-dt / gain_tau));

  // Visual compressor: preserves contrast above the threshold instead of
  // squeezing everything into the top few percent. The knee sits HIGH and the
  // ratio is gentle, so a loud band still ends up clearly taller than a medium
  // one instead of being levelled down to it.
  constexpr float kCompThreshold = 0.86F;
  constexpr float kCompRatio = 1.8F;

  // Contrast curve, applied to the DRAWN height only: take a display floor off
  // the top of the range and expand what is left. What sits under the floor is
  // display noise and goes to zero, the crowded middle is pushed down, and the
  // peaks keep their height -- that is what makes the columns move instead of
  // hovering together at a medium height.
  constexpr float kDisplayFloor = 0.07F;
  constexpr float kDisplayGamma = 1.30F;

  for (int band = 0; band < kCanonicalBands; ++band) {
    const auto index = static_cast<std::size_t>(band);
    const float driven =
        kernelled[index] * headroom_gain_;
    const float compressed =
        driven <= kCompThreshold
            ? driven
            : kCompThreshold + (driven - kCompThreshold) / kCompRatio;
    const float value = std::pow(
        std::clamp((std::clamp(compressed, 0.0F, 1.0F) - kDisplayFloor) /
                       (1.0F - kDisplayFloor),
                   0.0F, 1.0F),
        kDisplayGamma);

    fast[index] += (value - fast[index]) *
                   (value > fast[index] ? fast_rise : fast_fall);
    slow[index] += (value - slow[index]) *
                   (value > slow[index] ? slow_rise : slow_fall);
    // The mix leans on the FAST envelope, so the height follows the music's
    // own rhythm instead of gliding between sections.
    target[index] = 0.82F * fast[index] + 0.18F * slow[index];
  }

  // --- Spectral flux: gates how fast the spring rises, nothing else --------
  float flux = 0.0F;
  if (flux_enabled_ && motion_previous_.size() == position.size()) {
    float sum = 0.0F;
    for (std::size_t index = 0; index < position.size(); ++index)
      sum += std::max(0.0F, target[index] - motion_previous_[index]);
    flux = sum / static_cast<float>(position.size());
  }
  const float rise_boost =
      1.0F + std::clamp(flux * 6.0F, 0.0F, 0.20F); // at most +20%

  // --- Spring integration (parameters frozen this round) -------------------
  for (int band = 0; band < kCanonicalBands; ++band) {
    const auto index = static_cast<std::size_t>(band);
    const float error = target[index] - position[index];
    // Flux only accelerates the rise of bands that already have energy.
    const float stiffness =
        error > 0.0F ? kRiseStiffness * rise_boost : kFallStiffness;
    const float damping = error > 0.0F ? kRiseDamping : kFallDamping;
    velocity[index] += error * stiffness * step;
    velocity[index] *= std::exp(-damping * step);
    // Very light neighbour coupling: a rising cluster tugs its neighbours
    // without pulling the whole spectrum together.
    if (band > 0 && band + 1 < kCanonicalBands) {
      const float neighbour =
          0.5F * (position[index - 1] + position[index + 1]);
      velocity[index] += (neighbour - position[index]) * 2.0F * step;
    }
    position[index] += velocity[index] * step;
    position[index] = std::clamp(position[index], 0.0F, 1.03F);

    // Reflection: source is the displayed position, never raw FFT.
    const float mirror_target = position[index];
    mirror_state[index] +=
        (mirror_target - mirror_state[index]) * reflection_alpha;
  }

  if (!motion_test_)
    return;
  if (motion_previous_.size() == position.size()) {
    for (std::size_t index = 0; index < position.size(); ++index) {
      const float delta = position[index] - motion_previous_[index];
      const float magnitude = std::abs(delta);
      motion_sum_delta_ += magnitude;
      motion_max_delta_ =
          std::max(motion_max_delta_, static_cast<double>(magnitude));
      if (magnitude > 0.002F)
        ++motion_moving_bands_;
      if (index < motion_last_direction_.size() &&
          delta * motion_last_direction_[index] < 0.0F)
        ++motion_direction_changes_;
      if (index < motion_last_direction_.size())
        motion_last_direction_[index] = delta;
    }
  } else {
    motion_last_direction_.assign(position.size(), 0.0F);
  }
  motion_previous_ = position;
  ++motion_frames_;
  if (motion_frames_ % 15 == 0) {
    std::string row;
    for (const float value : position) {
      const int level =
          std::clamp(static_cast<int>(std::lround(value * 8.0F)), 0, 8);
      static constexpr char kGlyphs[] = " .:-=+*#%@";
      row.push_back(kGlyphs[level]);
    }
    std::fprintf(stderr, "FRAME %4d |%s|\n", motion_frames_, row.c_str());
    if (motion_frames_ >= 300) {
      const double frames = static_cast<double>(motion_frames_);
      std::vector<float> sorted(position);
      const auto pct = [&sorted](double q) {
        if (sorted.empty())
          return 0.0;
        const auto at = static_cast<std::size_t>(
            q * static_cast<double>(sorted.size() - 1));
        std::nth_element(sorted.begin(),
                         sorted.begin() + static_cast<std::ptrdiff_t>(at),
                         sorted.end());
        return static_cast<double>(sorted[at]);
      };
      std::fprintf(stderr,
                   "METRICS frames=%d meanDelta=%.5f maxDelta=%.4f "
                   "movingBands=%.1f%% directionChanges=%d P50=%.3f P75=%.3f "
                   "P95=%.3f hi80=%.1f%% hi90=%.1f%% hi95=%.1f%% "
                   "low20=%.1f%% flux=%.4f gain=%.3f\n",
                   motion_frames_, motion_sum_delta_ / frames,
                   motion_max_delta_,
                   100.0 * static_cast<double>(motion_moving_bands_) / frames /
                       static_cast<double>(position.size()),
                   motion_direction_changes_, pct(0.50), pct(0.75), pct(0.95),
                   100.0 * static_cast<double>(std::count_if(
                               position.begin(), position.end(),
                               [](float v) { return v >= 0.80F; })) /
                       static_cast<double>(position.size()),
                   100.0 * static_cast<double>(std::count_if(
                               position.begin(), position.end(),
                               [](float v) { return v >= 0.90F; })) /
                       static_cast<double>(position.size()),
                   100.0 * static_cast<double>(std::count_if(
                               position.begin(), position.end(),
                               [](float v) { return v >= 0.95F; })) /
                       static_cast<double>(position.size()),
                   100.0 * static_cast<double>(std::count_if(
                               position.begin(), position.end(),
                               [](float v) { return v <= 0.20F; })) /
                       static_cast<double>(position.size()),
                   static_cast<double>(flux),
                   static_cast<double>(headroom_gain_));
      motion_frames_ = 0;
      motion_sum_delta_ = 0.0;
      motion_max_delta_ = 0.0;
      motion_moving_bands_ = 0;
      motion_direction_changes_ = 0;
    }
  }
}

void Application::processTimers() {
  const auto now = std::chrono::steady_clock::now();
  // The tree MODEL follows the data, not the paint: the saved-playlist rows are
  // in place as soon as the playlists are known, so j/k and Enter act on the
  // tree the user is about to see even in a tick that happens before the next
  // frame. The signature check makes this a string comparison per tick.
  syncTreeFromPlaylists();
  // The UI owns the spectrum copy: the analyzer's callback only asks for a
  // repaint, so this works identically with or without a running screen loop.
  syncVisualizerSpectrum();
  // Cheap identity comparison on every tick: only a genuine track transition
  // reaches the resolver, and the resolver itself is idempotent per track.
  // While the search line owns the keyboard the screen is a static list, and
  // repainting it 30 times a second is exactly what makes a terminal IME's
  // composition flicker. The fast rate is therefore suspended for as long as
  // the box is open; closing it brings the animation straight back.
  const bool disc_animation = state_.display_mode == DisplayMode::Disc &&
                              disc_ && disc_->animationActive();
  const int animation_interval_ms =
      state_.display_mode == DisplayMode::Disc ? 67 : 33;
  ticker_fast_interval_ms_.store(animation_interval_ms);
  ticker_fast_.store(!search_prompt_ &&
                     (state_.player.state == PlaybackState::Playing ||
                      disc_animation || state_.demo || ui_slider_test_ ||
                      motion_test_ || state_.visualizer.data_available));
  // Top-bar clock. Refreshed at most once a second; the ticker is 250 ms.
  {
    if (state_.demo) {
      clock_text_ = "Mon Apr 28  21:42";
    } else {
      const std::time_t stamp = std::time(nullptr);
      if (stamp != last_clock_stamp_) {
        last_clock_stamp_ = stamp;
        std::tm local{};
        localtime_r(&stamp, &local);
        char buffer[64];
        // "Mon Apr 28  21:42" as in the reference.
        std::strftime(buffer, sizeof(buffer), "%a %b %d  %H:%M", &local);
        clock_text_ = buffer;
      }
    }
  }
  if (state_.toast && now - state_.toast_at > std::chrono::seconds(3)) {
    state_.toast.reset();
  }
  // Transient feedback: a changed message starts its own lifetime, and the
  // message is cleared when it expires. Nothing about it is permanent, so the
  // bottom row stays empty when there is nothing to say.
  if (visual_message_ != toast_message_) {
    toast_message_ = visual_message_;
    toast_at_ = now;
  } else if (!visual_message_.empty() &&
             now - toast_at_ > std::chrono::milliseconds(kToastMs)) {
    visual_message_.clear();
    toast_message_.clear();
  }
  // A binding capture commits after a short idle gap, which is what makes
  // multi-stroke sequences recordable while single keys still work.
  if (core::KeybindingsSection *table = keybindings();
      table != nullptr && table->captureIdleFor(now)) {
    table->commitCapture();
    syncKeymapMessage();
  }
  updateVisualizerMotion();

  // History is generated by playback, so it can grow while the user is
  // looking at it. Follow it without moving the cursor: the view is rebuilt
  // in place, and the renderer re-clamps the cursor to the new length.
  if (active_collection_ == ActiveCollection::History &&
      controller_.history().revision() != history_revision_seen_)
    refreshHistoryView();

  // Reference mode owns the whole state snapshot; never let the MPD watchdog
  // overwrite it or trigger a reconnect.
  if (state_.demo) {
    if (state_.toast && now - state_.toast_at > std::chrono::seconds(3))
      state_.toast.reset();
    return;
  }
  if (state_.mpd_connected && !backend_.connected()) {
    state_.mpd_connected = false;
    state_.error = backend_.lastError();
  }
  // Long-lived automatic reconnect, on a bounded low-frequency ladder
  // (3s, 5s, 10s, 15s, then 30s). It NEVER gives up: `auto_reconnect = true`
  // means "keep trying" until the server answers, the user turns it off, the
  // endpoint changes or the program exits. Each attempt is bounded by the
  // reachability probe, so the UI thread never waits on a dead address.
  if (controller_.connectionEpoch() != connection_epoch_seen_) {
    connection_epoch_seen_ = controller_.connectionEpoch();
    reconnect_attempts_ = 0;
    last_reconnect_ = now;
  }
  if (state_.mpd_connected) {
    reconnect_attempts_ = 0;
  } else if (controller_.config().auto_reconnect &&
             now - last_reconnect_ >=
                 reconnectDelay(reconnect_attempts_ + 1)) {
    last_reconnect_ = now;
    if (controller_.reconnect()) {
      reconnect_attempts_ = 0;
      startBackendEvents();
    } else {
      ++reconnect_attempts_;
    }
  }
}

void Application::dispatch(Action action) {
  switch (action) {
  case Action::Help:
    help_visible_ = !help_visible_;
    break;
  case Action::Quit:
    requestQuit();
    return;
  case Action::Search:
    if (state_.page == Page::Library &&
        workspace_pane_ == WorkspacePane::TrackList)
      openSearchPrompt();
    return;
  case Action::Cancel:
    // The overlay is dismissed by Cancel. Round 46 removed the old
    // hard-coded dismissal along with the legacy keyboard tail and did not
    // replace it, which left help openable but not closable.
    if (help_visible_) {
      help_visible_ = false;
      break;
    }
    controller_.clearSearch();
    return;
  case Action::Reconnect:
    if (controller_.reconnect())
      startBackendEvents();
    break;
  case Action::NewPlaylist:
    if (state_.page == Page::Library)
      openModal(Modal::NewPlaylist);
    return;
  case Action::RenamePlaylist:
    if (state_.page == Page::Library && collectionWritable())
      openModal(Modal::RenamePlaylist);
    return;
  case Action::DeletePlaylist:
    if (state_.page == Page::Library)
      openModal(Modal::DeletePlaylist);
    return;
  case Action::AddToPlaylist:
    if (state_.page == Page::Library)
      openModal(Modal::ChoosePlaylist);
    return;
  default:
    controller_.execute(action);
    break;
  }
  analyzer_.setPlaybackActive(state_.player.state == PlaybackState::Playing);
}

void Application::selectPage(int index) {
  // Immersive owns the interaction context: section navigation waits until it
  // is dismissed, so no background section change is ambiguous.
  if (state_.presentation == PresentationMode::ImmersiveNowPlaying)
    return;
  page_index_ = std::clamp(index, 0, kPageCount - 1);
  controller_.selectPage(allPages()[static_cast<std::size_t>(page_index_)]);
  focusCurrentList();
}

void Application::openModal(Modal modal) {
  // Rename and delete apply only to stored playlists; index 0 is Default.
  if ((modal == Modal::RenamePlaylist || modal == Modal::DeletePlaylist) &&
      (state_.library.database_view || state_.library.current <= 0 ||
       state_.library.current >=
           static_cast<int>(state_.library.playlists.size()))) {
    return;
  }
  if (modal == Modal::ChoosePlaylist) {
    playlist_target_rows_.assign(
        state_.library.playlists.begin() +
            std::min<std::size_t>(1, state_.library.playlists.size()),
        state_.library.playlists.end());
    playlist_target_ = 0;
  }
  if (modal == Modal::ChoosePlaylist && playlist_target_rows_.empty()) {
    modal = Modal::NewPlaylist;
  }
  modal_ = modal;
  modal_text_.clear();
  if (modal_ == Modal::RenamePlaylist) {
    modal_text_ =
        state_.library
            .playlists[static_cast<std::size_t>(state_.library.current)];
  }
  if (modal_ == Modal::NewPlaylist || modal_ == Modal::RenamePlaylist) {
    modal_input_->TakeFocus();
  } else if (modal_ == Modal::ChoosePlaylist) {
    playlist_target_menu_->TakeFocus();
  }
}

void Application::closeModal() {
  modal_ = Modal::None;
  modal_text_.clear();
  focusCurrentList();
}

void Application::confirmModal() {
  switch (modal_) {
  case Modal::NewPlaylist:
    controller_.createPlaylist(modal_text_);
    break;
  case Modal::RenamePlaylist:
    controller_.renameCurrentPlaylist(modal_text_);
    break;
  case Modal::DeletePlaylist:
    controller_.deleteCurrentPlaylist();
    break;
  case Modal::ChoosePlaylist:
    controller_.addSelectedToPlaylist(playlist_target_ + 1);
    break;
  case Modal::None:
    return;
  }
  closeModal();
}

void Application::focusCurrentList() {
  // The workspace (tree + table) is driven entirely by the keymap, so it only
  // needs a PASSIVE focus holder -- giving it a real widget would let that
  // widget swallow the global keys.
  //
  // `core`'s content pane is the opposite: sliders, toggles, text fields and
  // the binding table handle their own keys, so they must genuinely own the
  // focus or nothing there can be operated.
  const bool content_owns_keys =
      state_.page == Page::Settings &&
      workspace_pane_ == WorkspacePane::TrackList;
  if (content_owns_keys) {
    // The panel's tab hands focus to the selected entry's own widgets, which
    // is the only way they can be operated at all.
    if (core_panel_ != nullptr && core_panel_->component() != nullptr)
      core_panel_->component()->TakeFocus();
    return;
  }
  if (library_section_component_)
    library_section_component_->TakeFocus();
}

const TrackColumns &Application::activeColumns() const {
  // One column layout per collection kind. The Library's rows carry a file
  // size, History's carry the moment the track played, and a saved playlist's
  // carry exactly the tags `listplaylistinfo` reports.
  switch (active_collection_) {
  case ActiveCollection::Library:
    return metrics_.library_columns;
  case ActiveCollection::History:
    return metrics_.history_columns;
  case ActiveCollection::Playlist:
    break;
  }
  return metrics_.playlist_columns;
}

std::string Application::songRow(const Song &song, int ordinal, int width,
                                  bool playing) const {
  (void)width; // the column budget already comes from the live metrics
  return std::string(playing ? "▶" : " ") + " " + songRowBody(song, ordinal);
}

std::string Application::songRowBody(const Song &song, int ordinal) const {
  const SongRowParts parts = songRowParts(song, ordinal, activeColumns());
  return parts.ordinal + parts.icon + parts.title_lead + parts.title +
         parts.artist + parts.album + parts.duration + parts.size + parts.played;
}

Application::SongRowParts Application::songRowParts(
    const Song &song, int ordinal, const TrackColumns &columns) const {
  const bool header = ordinal < 0;
  SongRowParts parts;

  // Every part is padded to its column width and the LEADING separator is
  // carried by the column that follows it, so a blank value can never shift
  // the columns after it.
  const auto column = [&](int width, std::string value, int lead,
                          bool centred) -> std::string {
    if (width <= 0)
      return {};
    const std::string prefix(static_cast<std::size_t>(lead), ' ');
    const std::string body = util::ellipsize(std::move(value), width);
    return prefix + (centred ? util::padLeft(body, width)
                             : util::padRight(body, width));
  };

  if (columns.index > 0) {
    const std::string number = header ? "#" : std::to_string(ordinal);
    parts.ordinal = column(columns.index, number, 0, true) + " ";
  }
  if (columns.icon > 0) {
    // The header row leaves the icon column blank: the icon column carries no
    // label, which is what keeps the table minimal.
    parts.icon = column(columns.icon,
                        header ? std::string{} : iconGlyph(Icon::Music, icon_set_),
                        0, true);
  }
  if (columns.title > 0) {
    parts.title_lead = std::string(
        static_cast<std::size_t>(std::max(0, columns.title_lead)), ' ');
    parts.title = column(columns.title,
                         header ? "Title" : song.displayTitle(), 0, false);
  }
  if (columns.artist > 0) {
    // The header's own separator is part of the column, which is what lets the
    // data rows emit a blank artist and stay aligned.
    parts.artist = column(columns.artist,
                          header ? "Artist" : song.displayArtist(), 1, false);
  }
  if (columns.album > 0) {
    parts.album = column(columns.album,
                         header ? "Album" : song.displayAlbum(), 1, false);
  }
  if (columns.duration > 0) {
    parts.duration = column(
        columns.duration,
        header ? "Duration" : util::formatDuration(song.duration_seconds), 1,
        true);
  }
  if (columns.size > 0) {
    parts.size =
        column(columns.size,
               header ? "Size" : util::formatFileSize(song.file_size_bytes), 1,
               true);
  }
  if (columns.played > 0) {
    parts.played = column(
        columns.played,
        header ? "Played At" : util::formatPlayedAt(song.played_at_epoch), 1,
        false);
  }
  return parts;
}

Element Application::emptyState(std::string title, std::string detail) const {
  return vbox({text(std::move(title)) | bold | color(theme_.text),
               text(std::move(detail)) | color(theme_.muted_text)}) |
         center;
}

Element Application::panel(Element content, std::string title) const {
  if (!title.empty()) {
    content = vbox(
        {text(" " + std::move(title)) | bold | color(theme_.accent_primary),
         std::move(content) | flex});
  }
  return std::move(content) | borderStyled(ROUNDED, theme_.border);
}

} // namespace termusic::ui
