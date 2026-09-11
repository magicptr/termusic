#pragma once

// The `core` panel: every entry under the tree's second root, assembled.
//
// The Application owns one CorePanel and knows three things about it -- build
// it, render the current entry, and install its component in the tab tree.
// Which entries exist, in what order, and what each one contains is decided
// here, not in the Application.

#include <memory>
#include <string_view>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "ui/core/core.hpp"

namespace termusic::ui::core {

class CorePanel {
public:
  CorePanel();
  ~CorePanel();

  CorePanel(const CorePanel &) = delete;
  CorePanel &operator=(const CorePanel &) = delete;

  /// Builds every section's widgets. Must run after the Application's own
  /// component tree exists, because focus routing is shared.
  void build(const CoreContext &context);

  /// The tab container for the component tree: it owns focus and event
  /// routing, never the drawing (the Application renders `render()` itself).
  ftxui::Component component();

  /// Selects the entry whose `kCoreSections` index this is.
  void select(int index);
  int selected() const { return index_; }
  int size() const { return static_cast<int>(sections_.size()); }

  /// The selected entry's own name -- the pane heading and the tree label.
  std::string_view title() const;

  /// Draws the selected entry.
  ftxui::Element render(const CoreContext &context);

  /// True while the selected entry is taking typed text.
  bool editing() const;
  /// The label the selected entry's cursor is on ("" when it has none).
  std::string selectedSetting() const;
  /// True when the selected entry IS the binding table. The table owns a row
  /// list, so it is the one pane where j/k move inside the pane instead of
  /// walking the tree.
  bool keybindingsSelected() const { return index_ == kKeybindingsIndex; }
  int selectedIndex() const { return index_; }
  static constexpr int kKeybindingsIndex = 4;

  /// The one section with an interface beyond "draw yourself".
  KeybindingsSection *keybindings();

  /// The section at `index`, or nullptr. The Application drives the
  /// selected module through the generic SettingsSection interface.
  SettingsSection *section(int index);

private:
  std::vector<std::unique_ptr<SettingsSection>> sections_;
  ftxui::Component tab_;
  int index_ = 0;
};

} // namespace termusic::ui::core
