#include "ui/core/panel.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

namespace termusic::ui::core {

using namespace ftxui;

namespace {

/// THE SECTION REGISTRY.
///
/// Adding a settings module is two lines: one entry in `kCoreSections`
/// (state.hpp) for the tree row and its index, and one line here wiring that
/// index to the module's factory. The ids are checked against kCoreSections at
/// startup, so a mismatch is a loud failure rather than a pane that renders the
/// wrong module.
struct SectionFactory {
  std::string_view id;
  std::unique_ptr<SettingsSection> (*make)();
};

/// A covariant `unique_ptr` does not convert in a function-pointer context, so
/// each module gets a one-line adapter. This is the price of "one registration
/// line" and it is paid here, once.
std::unique_ptr<SettingsSection> makeGeneral() { return makeGeneralSection(); }
std::unique_ptr<SettingsSection> makeAppearance() {
  return makeAppearanceSection();
}
std::unique_ptr<SettingsSection> makeKeybindings() {
  return makeKeybindingsSection();
}
std::unique_ptr<SettingsSection> makeAbout() { return makeAboutSection(); }
std::unique_ptr<SettingsSection> makeHelp() { return makeHelpSection(); }

constexpr std::array<SectionFactory, kCoreSections.size()> kSectionFactories = {{
    {"core:general", makeGeneral},
    {"core:appearance", makeAppearance},
    {"core:keybindings", makeKeybindings},
    {"core:about", makeAbout},
    {"core:help", makeHelp},
}};

static_assert(kSectionFactories.size() == kCoreSections.size(),
              "every core section needs exactly one factory");

} // namespace

CorePanel::CorePanel() {
  // Section order follows kCoreSections, which the tree also renders from, so
  // a tree row and a tab index can never disagree.
  for (std::size_t index = 0; index < kSectionFactories.size(); ++index) {
    if (kSectionFactories[index].id != kCoreSections[index].id) {
      std::cerr << "core section registry mismatch at index " << index << ": "
                << kSectionFactories[index].id << " vs "
                << kCoreSections[index].id << std::endl;
      std::abort();
    }
    sections_.push_back(kSectionFactories[index].make());
  }
}

CorePanel::~CorePanel() = default;

void CorePanel::build(const CoreContext &context) {
  std::vector<Component> children;
  children.reserve(sections_.size());
  for (auto &section : sections_) {
    section->build(context);
    children.push_back(section->component());
  }
  tab_ = Container::Tab(std::move(children), &index_);
}

ftxui::Component CorePanel::component() { return tab_; }

void CorePanel::select(int index) {
  index_ = std::clamp(index, 0, static_cast<int>(sections_.size()) - 1);
}

std::string_view CorePanel::title() const {
  return sections_[static_cast<std::size_t>(index_)]->title();
}

ftxui::Element CorePanel::render(const CoreContext &context) {
  return sections_[static_cast<std::size_t>(index_)]->render(context);
}

bool CorePanel::editing() const {
  return sections_[static_cast<std::size_t>(index_)]->editing();
}

std::string CorePanel::selectedSetting() const {
  if (index_ < 0 || index_ >= static_cast<int>(sections_.size()))
    return {};
  const SettingsSection *entry =
      sections_[static_cast<std::size_t>(index_)].get();
  return entry != nullptr ? entry->selectedLabel() : std::string{};
}

SettingsSection *CorePanel::section(int index) {
  if (index < 0 || index >= static_cast<int>(sections_.size()))
    return nullptr;
  return sections_[static_cast<std::size_t>(index)].get();
}

KeybindingsSection *CorePanel::keybindings() {
  // The Application reaches the binding table through this one accessor rather
  // than through the section list, so the table stays the only section with an
  // interface of its own.
  for (auto &section : sections_) {
    if (auto *bindings = dynamic_cast<KeybindingsSection *>(section.get()))
      return bindings;
  }
  return nullptr;
}

} // namespace termusic::ui::core
