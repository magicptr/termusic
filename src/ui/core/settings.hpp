#pragma once

// The reusable settings system behind every `core` module.
//
// A module describes WHAT it offers -- a list of `SettingItem`s with getters
// and setters wired straight to the configuration -- and this header owns HOW
// that is navigated, edited and drawn. Nothing here knows about MPD, themes or
// plugins, and no module draws its own toggle, field or button.
//
// Adding a module is therefore: one new .cpp that fills a list, plus one line
// in the section registry (see panel.cpp).

#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "ui/core/core.hpp"

namespace termusic::ui::core {

/// What a settings row IS. Rendering and key handling are both driven by this,
/// so a new item type is one enumerator plus one branch here -- never a new
/// hand-built widget in a module.
enum class SettingKind {
  Heading,  ///< group title: read-only and NOT selectable
  Text,     ///< a line of text: selectable so a long block can be scrolled
  Toggle,   ///< on / off
  Input,    ///< free text
  Number,   ///< numeric: stepped with h/l, or typed after Enter
  Select,   ///< one of a fixed list: cycled with h/l or Enter
  Action,   ///< runs a callback
  Disabled, ///< shown but not offered: a reserved choice that does not exist
};

/// One row, with the accessors that read and write its value. The accessors are
/// the ONLY path to the configuration: the framework keeps no mirror of its
/// own, so a value changed anywhere shows up here on the next frame.
struct SettingItem {
  SettingKind kind = SettingKind::Text;
  std::string label;
  /// Optional second line explaining the setting (muted, wrapped by the caller).
  std::string help;

  std::function<bool()> get_bool;
  std::function<void(bool)> set_bool;
  std::function<std::string()> get_text;
  std::function<void(const std::string &)> set_text;
  std::function<int()> get_int;
  std::function<void(int)> set_int;
  std::function<int()> get_index;
  std::function<void(int)> set_index;
  std::function<void()> run;

  /// Select: the choices, in display order.
  std::vector<std::string> options;
  /// Number: bounds, step, and how the integer is presented.
  int minimum = 0;
  int maximum = 100;
  int step = 1;
  /// Number: 10 renders 15 as "1.5" (one decimal place).
  int scale = 1;
  /// Number: appended to the value, e.g. " s".
  std::string unit;
  /// Input: draw asterisks instead of the value.
  bool secret = false;

  /// True when this row can be activated at all. Headings and disabled rows
  /// cannot.
  bool selectable() const;
};

// --- Item builders: what a module actually writes --------------------------
SettingItem heading(std::string text);
SettingItem note(std::string text);
SettingItem note(std::function<std::string()> text);
SettingItem toggle(std::string label, std::function<bool()> get,
                   std::function<void(bool)> set, std::string help = {});
SettingItem input(std::string label, std::function<std::string()> get,
                  std::function<void(const std::string &)> set,
                  std::string help = {}, bool secret = false);
SettingItem number(std::string label, std::function<int()> get,
                   std::function<void(int)> set, int minimum, int maximum,
                   int step, std::string unit = {}, int scale = 1,
                   std::string help = {});
SettingItem select(std::string label, std::vector<std::string> options,
                   std::function<int()> get, std::function<void(int)> set,
                   std::string help = {});
SettingItem action(std::string label, std::function<void()> run,
                   std::string help = {});
/// A row that is SHOWN but not offered: a reserved choice whose implementation
/// does not exist yet. The cursor skips it, it never takes the focus band, and
/// its value is the fixed word `unavailable` -- so no key can select it.
SettingItem disabled(std::string label, std::string help = {});

/// The list engine: cursor, scrolling, editing and drawing for a list of
/// items. One per section; a section adds nothing else.
class SettingList {
public:
  void set(std::vector<SettingItem> items);
  const std::vector<SettingItem> &items() const { return items_; }
  std::size_t size() const { return items_.size(); }
  /// Controls whether moving past the first/last selectable row wraps around.
  /// Forms keep cyclic navigation; long read-only documents can clamp at their
  /// boundaries so reaching the end does not unexpectedly jump to the top.
  void setWrapNavigation(bool wrap) { wrap_navigation_ = wrap; }

  /// True while a field is taking typed text.
  bool editing() const { return editing_ >= 0; }
  /// True while a Select has its candidate curtain open. `j`/`k` then move the
  /// CANDIDATE cursor, not the settings cursor.
  bool curtainOpen() const { return open_select_ >= 0; }
  /// Closes whatever transient UI is open -- a field being typed into, or a
  /// Select curtain -- without changing any value. False when there is none, so
  /// the caller can fall through to the next step of its Esc handling.
  bool leaveEditing();

  void moveCursor(int delta);
  /// The label of the selected row, for a harness that drives by name.
  std::string selectedLabel() const;
  /// Enter. On a collapsed Select it opens the candidate curtain; on an open
  /// curtain it applies the highlighted candidate; on anything else it edits a
  /// field, flips a toggle or runs an action. It is the ONE key that changes a
  /// value, which is what leaves h/l with a single meaning: leaving and
  /// entering the pane.
  bool activate();
  /// One keystroke while a field is being edited.
  bool inputKey(const ftxui::Event &event);

  /// Draws the list. `rows` is the height the pane can give it, and `width`
  /// the cells a line may use -- text items wrap, so a description is readable
  /// at any terminal size.
  ftxui::Element render(const Theme &theme, bool focused, int rows,
                        int width) const;

private:
  /// The next selectable item from `from`, moving by `delta` (-1 / +1).
  int nextSelectable(int from, int delta) const;
  void scrollToCursor();
  /// The text a field currently shows (value, bullets, or the edit buffer).
  std::string displayValue(const SettingItem &item, int index) const;
  void commitEdit();

  std::vector<SettingItem> items_;
  int selected_ = 0;
  int scroll_ = 0;
  /// Index of the field being edited, or -1.
  int editing_ = -1;
  std::string buffer_;
  /// Index of the Select whose curtain is open, or -1, and the candidate the
  /// cursor points at inside it. Neither is configuration: the value only
  /// changes when Enter confirms, so Esc leaves the active value untouched.
  int open_select_ = -1;
  int candidate_ = 0;
  bool wrap_navigation_ = true;
};

/// A section that is nothing but a setting list: the shape almost every `core`
/// module has. A module derives from this, fills the list, and stops there.
class SettingsListSection : public SettingsSection {
public:
  void build(const CoreContext &context) override;
  ftxui::Component component() override;
  ftxui::Element render(const CoreContext &context) override;
  void poll(const CoreContext &context) override;

  bool editing() const override { return list_.editing(); }
  std::string selectedLabel() const override { return list_.selectedLabel(); }
  bool leaveEditing() override { return list_.leaveEditing(); }
  void moveCursor(int delta) override { list_.moveCursor(delta); }
  bool activate() override { return list_.activate(); }
  bool inputKey(const ftxui::Event &event) override {
    return list_.inputKey(event);
  }

protected:
  /// Fills the list. Called once at build(), and again on every frame where
  /// `itemsChanged()` says the list may differ.
  virtual void fill(const CoreContext &context) = 0;
  /// True when the item list itself may have changed (a plugin appeared, the
  /// theme list grew). Values never need this: they are read through getters.
  virtual bool itemsChanged(const CoreContext &context) const {
    (void)context;
    return false;
  }
  /// Lines per item, so a section can size its own content if it wants to.
  SettingList list_;
  const CoreContext *context_ = nullptr;
  Theme theme_;
  bool filled_ = false;
};

} // namespace termusic::ui::core
