#include "ui/core/core.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "util/text.hpp"

namespace termusic::ui::core {
namespace {

using namespace ftxui;

/// How long a multi-stroke binding may pause before it is committed. The
/// capture cannot use the runtime chord rules -- the binding being recorded may
/// not exist yet -- so completion is idle-based.
constexpr int kCaptureTimeoutMs = 1000;

/// The fixed chrome around the scrolling slice: the reset header, two rules,
/// the column header and the three-row detail footer, plus the panel border.
/// Slicing the rows first and appending chrome afterwards is what used to clip
/// controls off-screen on a short terminal.
constexpr int kChromeRows = 12;

/// Keybindings: the live table of every editable (context, action) pair.
///
/// This is the one `core` entry the rest of the application talks to, because
/// a key press outside the pane starts a capture inside it.
class KeybindingsTable final : public KeybindingsSection {
public:
  void build(const CoreContext &context) override {
    context_ = &context;
    theme_ = context.theme();
    // A Renderer with no widget child: the table draws itself and the
    // Application routes every key, so the component exists only to be a
    // focusable leaf inside the panel's tab container.
    component_ = Renderer([this, &context] { return render(context); });
    rebuild();
  }

  Component component() override { return component_; }

  // --- Rendering -----------------------------------------------------------
  void poll(const CoreContext &context) override {
    theme_ = context.theme();
    // A keymap change from anywhere else (a config reload, a plugin) must show
    // up here without a manual refresh; the row list is cheap to rebuild.
    if (context.keymap.revision() != revision_seen_) {
      revision_seen_ = context.keymap.revision();
      rebuild();
    }
  }

  Element render(const CoreContext &context) override {
    poll(context);
    const int count = static_cast<int>(rows_.size());
    const int visible = visibleRows(context);
    const int first = std::clamp(scroll_, 0, std::max(0, count - visible));
    // A band is the FOCUS marker, and this module has TWO focus areas: the
    // fixed reset control and the table. Exactly one of them may look focused,
    // and only while the pane itself owns the keyboard -- a remembered cursor
    // is not focus, so it must not draw a band.
    const bool pane_focused = context.content_focused();
    const bool reset_focused = pane_focused && focus_ == Focus::ResetFooter;
    const bool table_focused = pane_focused && focus_ == Focus::Rows;

    Elements column;
    Element reset = text("  \u21bb  Restore default keybindings");
    reset = reset_focused ? (reset | color(theme_.selected_fg) |
                             bgcolor(theme_.selected_bg) | bold)
                          : (reset | color(theme_.accent_primary));
    column.push_back(reset | reflect(reset_box_));
    column.push_back(text(util::repeat(70, "\u2500")) | color(theme_.border_dim));
    column.push_back(text("Context   Action                     Binding       "
                          "Source   Status") |
                     color(theme_.header_text) | bold);
    column.push_back(text(util::repeat(70, "\u2500")) | color(theme_.border));

    for (int index = first; index < count && index < first + visible; ++index) {
      const auto at = static_cast<std::size_t>(index);
      if (at >= cells_.size()) {
        // A row that somehow lost its cells is still drawn as a blank line, so
        // the table keeps its shape instead of collapsing.
        column.push_back(text(""));
        continue;
      }
      if (index == selected_) {
        // The band is this pane's FOCUS marker: it needs the pane to own the
        // keyboard AND the table to be the focus area inside it. A row the
        // cursor merely remembers never draws one.
        column.push_back(table_focused
                             ? text(display_rows_[at]) |
                                   color(theme_.selected_fg) |
                                   bgcolor(theme_.selected_bg)
                             : text(display_rows_[at]) | color(theme_.text));
        continue;
      }
      const Cells &row = cells_[at];
      // Status hues are text-only, never backgrounds:
      //   Source  -> Subtext0 (Default) / Lavender (Custom)
      //   Status  -> Green (Active)     / Yellow  (Shadowed)
      const Color origin_fg = row.origin.rfind("Custom", 0) == 0
                                  ? theme_.accent_purple
                                  : theme_.muted_text;
      const Color effect_fg = row.effect.rfind("Shadowed", 0) == 0
                                  ? theme_.warning
                                  : theme_.success;
      column.push_back(hbox({
          text(row.context) | color(theme_.muted_text),
          text(row.action) | color(theme_.text),
          text(row.binding) | color(theme_.accent_purple),
          text(row.origin) | color(origin_fg),
          text(row.effect) | color(effect_fg),
      }));
    }

    // --- Detail footer. Fixed height: only the text changes. ----------------
    column.push_back(text(util::repeat(70, "\u2500")) | color(theme_.border_dim));
    const KeymapRow *row =
        selected_ >= 0 && selected_ < count ? &rows_[static_cast<std::size_t>(selected_)]
                                            : nullptr;
    if (capture_) {
      column.push_back(hbox({
          text("RECORDING  ") | bold | color(theme_.accent_primary),
          text(std::string(actionLabel(capture_->action))) | color(theme_.text),
          filler(),
          text(capture_buffer_ + "_") | bold | color(theme_.accent_secondary),
      }));
      column.push_back(text("wait to save   Esc cancel") |
                       color(theme_.muted_text));
    } else if (row != nullptr) {
      column.push_back(hbox({
          text(bindingText(*row) + " \u2192 " +
               std::string(actionLabel(row->action))) |
              color(theme_.text),
          filler(),
          text(std::to_string(selected_ + 1) + " / " +
               std::to_string(count)) |
              color(theme_.weak_text),
      }));
      std::string status = originText(*row) + " \u00b7 " + effectText(*row);
      const std::vector<std::string> bindings =
          context.keymap.bindingsFor(row->context, row->action);
      if (!bindings.empty()) {
        const auto shadowed =
            context.keymap.shadowedIn(row->context, bindings.front());
        if (!shadowed.empty()) {
          // Shadow detail is clipped into the same fixed row, never added as
          // extra rows.
          status += "   shadowed: " +
                    std::string(contextName(shadowed.front().first)) +
                    " \u2192 " +
                    std::string(actionLabel(shadowed.front().second));
          if (shadowed.size() > 1)
            status += ", +" + std::to_string(shadowed.size() - 1) + " more";
        }
      }
      const std::string &message = context.transient_message();
      if (!message.empty())
        status = message;
      column.push_back(text(util::ellipsize(status, 70)) |
                       color(theme_.muted_text));
    } else {
      column.push_back(text(""));
      column.push_back(text(""));
    }
    // Only the keys that still exist are advertised: the per-row reset is the
    // fixed footer control's job now, so `r` is not named here any more.
    column.push_back(text("Enter rebind") | color(theme_.muted_text));
    return vbox(std::move(column));
  }

  std::string_view title() const override { return "Keybindings"; }

  // --- Interface used by the Application -----------------------------------
  bool capturing() const override { return capture_.has_value(); }
  std::string_view focusArea() const override {
    return focus_ == Focus::ResetFooter ? "reset" : "table";
  }
  int cursorIndex() const override {
    return rows_.empty() ? -1 : selected_;
  }
  const std::string &captureBuffer() const override { return capture_buffer_; }
  bool resetPending() const override { return reset_pending_; }

  bool armResetSelected() override {
    if (focus_ != Focus::ResetFooter)
      return false;
    reset_pending_ = true;
    return true;
  }

  bool beginCapture() override {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size()))
      return false;
    capture_ = rows_[static_cast<std::size_t>(selected_)];
    capture_buffer_.clear();
    return true;
  }

  bool commitCapture() override {
    if (!capture_ || capture_buffer_.empty()) {
      cancelCapture();
      return false;
    }
    const KeymapRow row = *capture_;
    const std::string sequence = capture_buffer_;
    cancelCapture();

    // One validation authority: the captured sequence goes through the same
    // override path the configuration loader uses, so an impossible binding is
    // refused here rather than at the next launch.
    std::string problem;
    if (!context_->preview_binding(row.context, row.action, {sequence},
                                   &problem)) {
      message_ = "Cannot bind \"" + sequence + "\": " + problem;
      return false;
    }
    if (!context_->save_binding(row.context, row.action, {sequence}))
      message_ = "Saved for this session, but the config file could not be written";
    else
      message_ = "Bound " + std::string(actionLabel(row.action)) + " to \"" +
                 sequence + "\"";
    rebuild();
    return true;
  }

  void cancelCapture() override {
    capture_.reset();
    capture_buffer_.clear();
  }

  void cancelReset() override { reset_pending_ = false; }

  void moveCursor(int delta) override {
    const int last = std::max(0, static_cast<int>(rows_.size()) - 1);
    // The reset control sits ABOVE the table, so it is the row before the
    // first binding: k from the top reaches it, j from it returns.
    if (focus_ == Focus::ResetFooter) {
      if (delta > 0) {
        // Entering the table resumes where the user left it; only an empty
        // table starts at zero. Leaving the table never clears the cursor, so
        // "remembered row" and "focused row" stay different things.
        if (rows_.empty())
          selected_ = 0;
        focus_ = Focus::Rows;
        scrollToCursor();
      }
      return;
    }
    if (delta < 0 && selected_ <= 0) {
      focus_ = Focus::ResetFooter;
      return;
    }
    selected_ = std::clamp(selected_ + delta, 0, last);
    scrollToCursor();
  }

  /// Arms the restore-defaults confirmation. Recovery must never depend on a
  /// configurable shortcut, so this is a named operation rather than a key.
  void armReset() {
    if (focus_ == Focus::ResetFooter)
      reset_pending_ = true;
  }

  /// The footer's hit box, for the pre-event mouse path: FTXUI's Tab container
  /// claims clicks inside its own region before the normal path sees them.
  bool hitsResetFooter(const Mouse &mouse) const override {
    return !reset_box_.IsEmpty() && reset_box_.Contain(mouse.x, mouse.y);
  }

  /// Starts a capture from the footer click path.
  void focusFooter() override { focus_ = Focus::ResetFooter; }

  /// Rebinds the selected row back to its built-in default.
  void resetSelected() override {
    if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size()))
      return;
    const KeymapRow row = rows_[static_cast<std::size_t>(selected_)];
    if (!context_->keymap.resetBinding(row.context, row.action)) {
      message_ = "Already using the default binding";
      return;
    }
    (void)context_->save_binding(row.context, row.action, {});
    message_ =
        "Reset " + std::string(actionLabel(row.action)) + " to default";
    rebuild();
  }

  void appendCaptureToken(const std::string &token) override {
    if (!capture_ || token.empty())
      return;
    if (!capture_buffer_.empty())
      capture_buffer_ += ' ';
    capture_buffer_ += token;
    capture_started_ = std::chrono::steady_clock::now();
  }

  bool captureIdleFor(std::chrono::steady_clock::time_point now) const override {
    return capture_ && !capture_buffer_.empty() &&
           now - capture_started_ > std::chrono::milliseconds(kCaptureTimeoutMs);
  }

  /// Hands the last message to the caller and clears it, so one message is
  /// reported once.
  std::string takeMessage() override {
    std::string result = std::move(message_);
    message_.clear();
    return result;
  }

  void clearMessage() { message_.clear(); }
  void setMessage(std::string message) { message_ = std::move(message); }
  /// Arms the reset confirmation without a key press (the footer click path).
  void requestReset() {
    reset_pending_ = true;
    message_.clear();
  }

private:
  struct Cells {
    std::string context;
    std::string action;
    std::string binding;
    std::string origin;
    std::string effect;
  };

  enum class Focus { Rows, ResetFooter };

  int visibleRows(const CoreContext &context) const {
    return std::max(2, context.metrics.main_height - kChromeRows);
  }

  void scrollToCursor() {
    const int visible = std::max(2, context_->metrics.main_height - kChromeRows);
    const int count = static_cast<int>(rows_.size());
    const int last = std::max(0, count - visible);
    if (selected_ < scroll_)
      scroll_ = selected_;
    if (selected_ >= scroll_ + visible)
      scroll_ = selected_ - visible + 1;
    scroll_ = std::clamp(scroll_, 0, last);
  }

  std::string bindingText(const KeymapRow &row) const {
    const std::vector<std::string> bindings =
        context_->keymap.bindingsFor(row.context, row.action);
    if (bindings.empty())
      return "\u2014";
    std::string rendered;
    for (const std::string &sequence : bindings) {
      if (!rendered.empty())
        rendered += ", ";
      rendered += sequence;
    }
    return rendered;
  }

  std::string originText(const KeymapRow &row) const {
    // Origin and effect are separate columns on purpose: a binding can be
    // Custom *and* Shadowed, and collapsing them loses one of the facts.
    return context_->keymap.originOf(row.context, row.action) ==
                   Keymap::Origin::Custom
               ? "Custom"
               : "Default";
  }

  std::string effectText(const KeymapRow &row) const {
    return context_->keymap.effectOf(row.context, row.action) ==
                   Keymap::Effect::Shadowed
               ? "Shadowed"
               : "Active";
  }

  void rebuild() {
    rows_.clear();
    for (const KeyContext context : allContexts()) {
      for (const Action action : allActions()) {
        // System/recovery actions are not ordinary rows: rebinding them through
        // the same Enter-to-capture path as a normal action is exactly the
        // failure that recovery must not have.
        if (!actionEditable(action))
          continue;
        // An entry the application no longer configures in this context gets no
        // row at all -- not an empty one. The ActionInfo metadata is the
        // authority, so a removed shortcut can never leave a blank row behind.
        if (!actionConfigurableIn(action, context))
          continue;
        if (context_->keymap.bindingsFor(context, action).empty())
          continue;
        rows_.push_back(KeymapRow{context, action});
      }
    }
    if (!rows_.empty())
      selected_ = std::clamp(selected_, 0, static_cast<int>(rows_.size()) - 1);
    scrollToCursor();

    display_rows_.clear();
    cells_.clear();
    display_rows_.reserve(rows_.size());
    cells_.reserve(rows_.size());
    for (const KeymapRow &row : rows_) {
      Cells cells;
      cells.context = util::padRight(std::string(contextName(row.context)), 11);
      cells.action = util::padRight(std::string(actionLabel(row.action)), 26);
      cells.binding = util::padRight(bindingText(row), 18);
      cells.origin = util::padRight(originText(row), 9);
      cells.effect = effectText(row);
      display_rows_.push_back(cells.context + cells.action + cells.binding +
                              cells.origin + cells.effect);
      cells_.push_back(std::move(cells));
    }
    if (context_ != nullptr)
      revision_seen_ = context_->keymap.revision();
  }

  const CoreContext *context_ = nullptr;
  Theme theme_;
  Component component_;
  std::vector<KeymapRow> rows_;
  std::vector<std::string> display_rows_;
  std::vector<Cells> cells_;
  int selected_ = 0;
  int scroll_ = 0;
  Focus focus_ = Focus::Rows;
  bool reset_pending_ = false;
  std::optional<KeymapRow> capture_;
  std::string capture_buffer_;
  std::chrono::steady_clock::time_point capture_started_{};
  std::string message_;
  std::size_t revision_seen_ = static_cast<std::size_t>(-1);
  Box reset_box_;
};

} // namespace

std::unique_ptr<KeybindingsSection> makeKeybindingsSection() {
  return std::make_unique<KeybindingsTable>();
}

} // namespace termusic::ui::core
