#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace termusic::extensions {

enum class SettingKind { Boolean, Integer, Number, String, Choice };

struct SettingDescriptor {
  std::string id;
  std::string section;
  std::string label;
  std::string description;
  SettingKind kind = SettingKind::String;
  std::string default_value;
  std::vector<std::string> choices;
  double minimum = 0.0;
  double maximum = 0.0;
  bool restart_required = false;
};

struct UiBlock {
  std::string id;
  /// Named placement point, initially "settings.extensions". New built-in UI
  /// modules can add slots without changing the plugin ABI.
  std::string slot;
  std::string title;
  int order = 0;
  std::function<std::string()> render_text;
};

class ExtensionRegistry {
public:
  bool registerSetting(SettingDescriptor descriptor,
                       std::string *error = nullptr);
  bool registerUiBlock(UiBlock block, std::string *error = nullptr);

  const std::vector<SettingDescriptor> &settings() const { return settings_; }
  std::vector<std::reference_wrapper<const UiBlock>>
  blocksFor(std::string_view slot) const;

private:
  std::vector<SettingDescriptor> settings_;
  std::vector<UiBlock> blocks_;
};

} // namespace termusic::extensions
