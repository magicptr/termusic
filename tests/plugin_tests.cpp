#include <cassert>
#include <filesystem>
#include <map>
#include <string>

#include "extensions/extension_registry.hpp"
#include "extensions/plugin_manager.hpp"
#include "ui/theme.hpp"

int main(int argc, char **argv) {
  assert(argc == 2);
  termusic::ui::ThemeRegistry themes;
  termusic::extensions::ExtensionRegistry extensions;
  const std::map<std::string, std::map<std::string, std::string>> settings = {
      {"fixture", {{"greeting", "你好"}}}};
  termusic::extensions::PluginManager plugins(extensions, themes, settings);
  assert(plugins.load(std::filesystem::path(argv[1])));
  assert(plugins.plugins().size() == 1U);
  assert(plugins.plugins().front().id == "fixture");
  assert(extensions.settings().size() == 1U);
  assert(extensions.settings().front().id == "fixture.greeting");
  assert(themes.resolveId("fixture-dark") == "fixture-dark");
  const auto blocks = extensions.blocksFor("settings.extensions");
  assert(blocks.size() == 1U);
  assert(blocks.front().get().render_text() == "Greeting: 你好");
  // Loading the same id again is rejected transactionally: the first
  // plugin's contributions remain exactly once.
  assert(!plugins.load(std::filesystem::path(argv[1])));
  assert(plugins.plugins().size() == 1U);
  assert(extensions.settings().size() == 1U);
  assert(themes.resolveId("fixture-dark") == "fixture-dark");
  assert(extensions.blocksFor("settings.extensions").size() == 1U);
  return 0;
}
