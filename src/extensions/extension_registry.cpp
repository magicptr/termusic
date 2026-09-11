#include "extensions/extension_registry.hpp"

#include <algorithm>
#include <cctype>

namespace termusic::extensions {
namespace {

bool validId(std::string_view id) {
  return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '.' || c == '-' || c == '_';
  });
}

} // namespace

bool ExtensionRegistry::registerSetting(SettingDescriptor descriptor,
                                        std::string *error) {
  if (!validId(descriptor.id) || descriptor.label.empty()) {
    if (error)
      *error = "setting requires a valid id and non-empty label";
    return false;
  }
  if (std::any_of(settings_.begin(), settings_.end(), [&](const auto &entry) {
        return entry.id == descriptor.id;
      })) {
    if (error)
      *error = "duplicate setting id: " + descriptor.id;
    return false;
  }
  settings_.push_back(std::move(descriptor));
  return true;
}

bool ExtensionRegistry::registerUiBlock(UiBlock block, std::string *error) {
  if (!validId(block.id) || block.slot.empty() || !block.render_text) {
    if (error)
      *error = "UI block requires an id, slot and render callback";
    return false;
  }
  if (std::any_of(blocks_.begin(), blocks_.end(),
                  [&](const auto &entry) { return entry.id == block.id; })) {
    if (error)
      *error = "duplicate UI block id: " + block.id;
    return false;
  }
  blocks_.push_back(std::move(block));
  return true;
}

std::vector<std::reference_wrapper<const UiBlock>>
ExtensionRegistry::blocksFor(std::string_view slot) const {
  std::vector<std::reference_wrapper<const UiBlock>> result;
  for (const auto &block : blocks_) {
    if (block.slot == slot)
      result.emplace_back(block);
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const auto &lhs, const auto &rhs) {
                     return lhs.get().order < rhs.get().order;
                   });
  return result;
}

} // namespace termusic::extensions
