#include "ui/core/core.hpp"
#include "ui/core/settings.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace termusic::ui::core {
namespace {

/// Plugins: what the extension registry actually loaded.
///
/// Read-only on purpose, but built from the same setting items, so it scrolls
/// and navigates like every other module. A plugin's own settings belong to the
/// plugin (it declares them through the extension API) and are edited with its
/// own widgets; inventing a second, core-owned editor here would let the two
/// disagree.
class PluginsSection final : public SettingsListSection {
public:
  std::string_view title() const override { return "Plugins"; }

protected:
  bool itemsChanged(const CoreContext &context) const override {
    // The registry can grow while the application runs (a directory is
    // rescanned), and the list must follow it.
    return signature_ != signatureOf(context);
  }

  void fill(const CoreContext &context) override {
    signature_ = signatureOf(context);
    std::vector<SettingItem> items;
    for (const auto &setting : context.extensions.settings()) {
      items.push_back(heading(setting.label));
      if (!setting.description.empty())
        items.push_back(note(setting.description));
      items.push_back(note("id  " + setting.id));
    }
    for (const auto &reference : context.extensions.blocksFor("settings.extensions")) {
      const auto &block = reference.get();
      if (!block.title.empty())
        items.push_back(heading(block.title));
      std::istringstream content(block.render_text());
      std::string line;
      while (std::getline(content, line))
        items.push_back(note(line));
    }
    if (items.empty()) {
      items.push_back(heading("No plugins loaded"));
      items.push_back(note("Drop a native plugin into the configured plugin"));
      items.push_back(note("directory, then restart termusic."));
    }
    list_.set(std::move(items));
  }

private:
  static std::string signatureOf(const CoreContext &context) {
    std::string signature;
    for (const auto &setting : context.extensions.settings())
      signature += setting.id + "\n";
    for (const auto &reference :
         context.extensions.blocksFor("settings.extensions"))
      signature += reference.get().render_text() + "\n";
    return signature;
  }

  std::string signature_;
};

} // namespace

std::unique_ptr<SettingsSection> makePluginsSection() {
  return std::make_unique<PluginsSection>();
}

} // namespace termusic::ui::core
