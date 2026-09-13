#pragma once

// `core` is the configuration half of the workspace: the tree's second root,
// whose entries are settings, not text files. This header is the module
// boundary for that whole side of the application.
//
// Each entry under `core` is one self-contained module (`general`, `mpd`,
// `appearance`, `keybindings`, `plugins`). The set and its order are exactly
// `kCoreSections`, so the tree and the panes cannot drift apart. A module owns
//
//   * the widgets its pane shows,
//   * the UI-local mirror of the settings it edits,
//   * the code that writes a change back through the Controller,
//   * its own title.
//
// What it does NOT own is navigation, theming primitives and application
// state: those arrive through CoreContext. Nothing here reaches into
// Application, so a section can be read, tested and edited on its own -- and a
// new entry under `core` is one new file plus one line in kCoreSections.

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "app/actions.hpp"
#include "app/keymap.hpp"
#include "app/state.hpp"
#include "config/config.hpp"
#include "controller/controller.hpp"
#include "extensions/extension_registry.hpp"
#include "ui/metrics.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace termusic::ui::core {

/// One editable (context, action) pair. Rows are derived from ActionInfo plus
/// the live Keymap -- there is no second action list.
struct KeymapRow {
  KeyContext context;
  Action action;
};

/// Everything a `core` entry is allowed to use, and nothing more.
///
/// References are non-owning on purpose: the Application outlives every
/// section, and a section must never keep application state alive.
struct CoreContext {
  AppState &state;
  Controller &controller;
  extensions::ExtensionRegistry &extensions;
  ThemeRegistry &themes;
  /// Live terminal metrics, re-derived every frame. A section reads geometry
  /// from here instead of inventing its own widths.
  const UiMetrics &metrics;

  /// The page the General section cycles through on request.
  const std::function<int()> &start_page_index;
  const std::function<void(int)> &set_start_page_index;

  /// Persists the current configuration. One entry point, so a section can
  /// never write half a change.
  const std::function<bool()> &save_config;

  /// Re-resolves the active theme after it was changed. The Application owns
  /// `theme_`, so the section reports the new id and the Application applies
  /// it.
  const std::function<void()> &theme_changed;

  /// Rebuilds the analyzer after a visualizer setting changed.
  const std::function<void()> &visualizer_changed;

  /// Asks MPD to rescan its music directory.
  const std::function<void()> &update_database;

  /// Where the configuration file is, for the Help module's "config" line.
  const std::function<std::string()> &config_path;

  // --- The binding table's view of the application -------------------------
  /// True while the CONTENT pane owns the keyboard. The table paints its
  /// selection band only when it does, which is what keeps at most one strong
  /// highlight on screen.
  const std::function<bool()> &content_focused;
  /// Transient one-line feedback from the last bind/unbind/reset.
  const std::function<const std::string &()> &transient_message;
  /// The live keymap. The table rebinds through it and the application
  /// dispatches through it, so there is exactly one authority.
  Keymap &keymap;
  /// Validates a candidate override without committing it.
  const std::function<bool(KeyContext, Action, const std::vector<std::string> &,
                           std::string *)> &preview_binding;
  /// Persists one override (or one removal) through the Controller.
  const std::function<bool(KeyContext, Action,
                           const std::vector<std::string> &)> &save_binding;
  const std::function<void()> &reset_bindings;

  /// Reports that the MPD connection fields were edited and the new settings
  /// are ready to be applied (including a reconnect when one is warranted).
  /// The values travel with the call: the section owns the text fields, so it
  /// is the only place that knows what was typed.
  const std::function<void(std::string host, int port, std::string password)>
      &connection_changed;

  /// The shared compact-button chrome, so every section's controls look alike.
  const std::function<ftxui::ButtonOption()> &compact_button;

  // --- Shorthands the sections use constantly ------------------------------
  Config &config() const { return controller.config(); }
  Theme theme() const { return themes.resolve(controller.config().theme_name); }
};

/// One entry under `core`.
class SettingsSection {
public:
  virtual ~SettingsSection() = default;

  /// Builds the widgets once, after the Application's component tree exists.
  /// The context reference stays valid for the section's whole lifetime, so a
  /// callback may capture its address.
  virtual void build(const CoreContext &context) = 0;
  /// The component the panel installs in its tab. Focus and event routing go
  /// through it, so a section that owns text fields must return the container
  /// holding them -- returning a leaf would make the others unreachable.
  virtual ftxui::Component component() = 0;
  /// The pane's content. Called on every frame while this entry is open.
  virtual ftxui::Element render(const CoreContext &context) = 0;
  /// Adopts values that changed outside this section (a config reload, a
  /// reconnect, a keymap reset). Called once per frame; cheap and idempotent.
  virtual void poll(const CoreContext &context) = 0;
  /// The name shown as the pane's heading -- identical to the tree label.
  virtual std::string_view title() const = 0;
  /// True while a TEXT FIELD in this entry owns the keyboard. Only those
  /// consume printable keys, so this is the one case where the global keymap
  /// must stand aside; a focused button or slider must not block it.
  virtual bool editing() const { return false; }

  // --- Generic pane interaction --------------------------------------------
  // The Application routes the pane's keys here instead of letting a section
  // think about physical keys. A section built on SettingList overrides these
  // in one line; a section with an interface of its own (the binding table)
  // leaves them and is handled explicitly.
  /// Moves the pane's own cursor (j / k, arrows).
  virtual void moveCursor(int delta) { (void)delta; }
  /// The label the pane's cursor is on, or empty when the pane has no rows.
  /// Exposed for the scripted harness: driving a settings row by NAME keeps a
  /// test honest when the list gains or loses an entry.
  virtual std::string selectedLabel() const { return {}; }
  /// Enter: flip a toggle, edit a field, run an action, cycle a choice. This
  /// is the only key that changes a value, which leaves h/l with one meaning:
  /// leaving and entering the pane.
  virtual bool activate() { return false; }
  /// One keystroke while `editing()` is true.
  virtual bool inputKey(const ftxui::Event &event) {
    (void)event;
    return false;
  }
  /// Leaves a field without committing. False when nothing was being edited.
  virtual bool leaveEditing() { return false; }
};

std::unique_ptr<SettingsSection> makeGeneralSection();
std::unique_ptr<SettingsSection> makeAppearanceSection();
std::unique_ptr<SettingsSection> makePluginsSection();
std::unique_ptr<SettingsSection> makeAboutSection();
std::unique_ptr<SettingsSection> makeHelpSection();

/// The binding table, which is the one `core` entry the rest of the UI has to
/// talk to: a key press outside the pane starts a capture inside it, and the
/// recovery footer is hit-tested before the component tree sees the click.
class KeybindingsSection : public SettingsSection {
public:
  /// True while a new binding is being recorded: no other key may act.
  virtual bool capturing() const = 0;
  /// Starts capturing for the selected row. Returns false when there is none.
  virtual bool beginCapture() = 0;
  /// Enter on the reset control arms the restore-defaults confirmation.
  /// Returns false when the cursor is on a table row instead.
  virtual bool armResetSelected() = 0;
  /// Applies a completed capture. Returns false when the binding was refused.
  virtual bool commitCapture() = 0;
  virtual void cancelCapture() = 0;
  /// The sequence being typed, for the recording readout.
  virtual const std::string &captureBuffer() const = 0;
  /// Appends one normalized key token to the recording.
  virtual void appendCaptureToken(const std::string &token) = 0;
  /// True when the recording has been idle long enough to commit.
  virtual bool captureIdleFor(std::chrono::steady_clock::time_point now)
      const = 0;
  /// Rebinds the selected row back to its built-in default.
  virtual void resetSelected() = 0;
  /// Hands the last message to the caller and clears it (report once).
  virtual std::string takeMessage() = 0;
  /// The fixed recovery footer, hit-tested before the component tree sees a
  /// click inside the panel (FTXUI's Tab claims its own region first).
  virtual bool hitsResetFooter(const ftxui::Mouse &mouse) const = 0;
  /// Moves the table cursor onto the reset control.
  virtual void focusFooter() = 0;
  /// The "restore defaults" confirmation, which the Application owns because
  /// it is a modal state: Enter confirms, Esc cancels, and every other key is
  /// swallowed while it is pending.
  virtual void cancelReset() = 0;
  virtual void requestReset() = 0;
  /// Which of the two focus areas owns the keyboard right now, as a stable
  /// token ("reset" / "table"). Reported so a test can assert focus ownership
  /// and cursor memory without reading pixels.
  virtual std::string_view focusArea() const = 0;
  /// The row the table REMEMBERS (its index), or -1 when it has no rows. A
  /// remembered row is not focus: it carries no band unless `focusArea()` says
  /// the table owns the keyboard.
  virtual int cursorIndex() const = 0;
  virtual bool resetPending() const = 0;
  /// Moves the table cursor by one row.
  virtual void moveCursor(int delta) = 0;
};

std::unique_ptr<KeybindingsSection> makeKeybindingsSection();

/// The module header every `core` pane shares: the entry's own name, a rule,
/// then the content. Exported because every section must draw the same one.
ftxui::Element corePane(const std::string &title, ftxui::Element content,
                        const Theme &theme);

/// A labelled form row: a fixed-width caption, then the control. One geometry
/// for every setting, so a pane reads as a form and not as a pile of widgets.
ftxui::Element coreRow(std::string label, ftxui::Element control,
                       const Theme &theme);

} // namespace termusic::ui::core
