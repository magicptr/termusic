#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "extensions/extension_registry.hpp"
#include "extensions/plugin_api.h"
#include "ui/theme.hpp"

namespace termusic::extensions {

struct PluginInfo {
  std::string id;
  std::string name;
  std::string version;
  std::filesystem::path path;
};

/// Loads native extensions through a versioned C ABI. Shared objects remain
/// loaded for this object's lifetime, so registered render callbacks stay
/// valid.
class PluginManager {
public:
  PluginManager(ExtensionRegistry &extensions, ui::ThemeRegistry &themes,
                const std::map<std::string, std::map<std::string, std::string>>
                    &settings);
  ~PluginManager();

  PluginManager(const PluginManager &) = delete;
  PluginManager &operator=(const PluginManager &) = delete;

  std::size_t loadDirectory(const std::filesystem::path &directory);
  bool load(const std::filesystem::path &path);

  const std::vector<PluginInfo> &plugins() const { return plugin_infos_; }
  const std::vector<std::string> &warnings() const { return warnings_; }

private:
  struct Bridge;
  struct LoadedPlugin {
    void *library = nullptr;
    const termusic_plugin_v1 *descriptor = nullptr;
  };

  std::unique_ptr<Bridge> bridge_;
  termusic_plugin_host_v1 host_{};
  std::vector<LoadedPlugin> loaded_;
  std::vector<PluginInfo> plugin_infos_;
  std::vector<std::string> warnings_;
};

} // namespace termusic::extensions
