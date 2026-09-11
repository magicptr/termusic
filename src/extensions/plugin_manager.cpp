#include "extensions/plugin_manager.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

#include <dlfcn.h>

namespace termusic::extensions {
namespace {

ftxui::Color color(termusic_rgb_v1 value) {
  return ftxui::Color::RGB(value.red, value.green, value.blue);
}

ui::Theme convertTheme(const termusic_theme_v1 &source) {
  ui::Theme out;
#define TERMUSIC_COLOR(field) out.field = color(source.field)
  TERMUSIC_COLOR(background);
  TERMUSIC_COLOR(background_deep);
  TERMUSIC_COLOR(panel);
  TERMUSIC_COLOR(text);
  TERMUSIC_COLOR(muted_text);
  TERMUSIC_COLOR(weak_text);
  TERMUSIC_COLOR(header_text);
  TERMUSIC_COLOR(border);
  TERMUSIC_COLOR(border_dim);
  TERMUSIC_COLOR(separator);
  TERMUSIC_COLOR(accent_primary);
  TERMUSIC_COLOR(accent_secondary);
  TERMUSIC_COLOR(accent_purple);
  TERMUSIC_COLOR(selected_fg);
  TERMUSIC_COLOR(selected_bg);
  TERMUSIC_COLOR(hover_bg);
  TERMUSIC_COLOR(hover_border);
  TERMUSIC_COLOR(progress_filled);
  TERMUSIC_COLOR(progress_filled_end);
  TERMUSIC_COLOR(progress_empty);
  TERMUSIC_COLOR(icon);
  TERMUSIC_COLOR(progress_knob);
  TERMUSIC_COLOR(spectrum_low);
  TERMUSIC_COLOR(spectrum_high);
  TERMUSIC_COLOR(spectrum_peak);
  TERMUSIC_COLOR(error);
  TERMUSIC_COLOR(ok);
#undef TERMUSIC_COLOR
  return out;
}

std::vector<std::string> splitCsv(const char *input) {
  std::vector<std::string> result;
  if (!input)
    return result;
  std::istringstream stream(input);
  std::string item;
  while (std::getline(stream, item, ',')) {
    const auto first = item.find_first_not_of(" \t");
    const auto last = item.find_last_not_of(" \t");
    if (first != std::string::npos)
      result.push_back(item.substr(first, last - first + 1));
  }
  return result;
}

SettingKind convertKind(termusic_setting_kind_v1 kind) {
  switch (kind) {
  case TERMUSIC_SETTING_BOOL_V1:
    return SettingKind::Boolean;
  case TERMUSIC_SETTING_INTEGER_V1:
    return SettingKind::Integer;
  case TERMUSIC_SETTING_NUMBER_V1:
    return SettingKind::Number;
  case TERMUSIC_SETTING_CHOICE_V1:
    return SettingKind::Choice;
  case TERMUSIC_SETTING_STRING_V1:
  default:
    return SettingKind::String;
  }
}

bool validPluginId(std::string_view id) {
  return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return std::isalnum(c) != 0 || c == '.' || c == '-' || c == '_';
  });
}

} // namespace

struct PluginManager::Bridge {
  struct PendingTheme {
    std::string id;
    std::string name;
    ui::Theme theme;
  };

  ExtensionRegistry &extensions;
  ui::ThemeRegistry &themes;
  std::map<std::string, std::map<std::string, std::string>> settings;
  std::filesystem::path loading_path;
  std::string last_error;
  std::vector<PendingTheme> pending_themes;
  std::vector<SettingDescriptor> pending_settings;
  std::vector<UiBlock> pending_blocks;

  void beginLoad(const std::filesystem::path &path) {
    loading_path = path;
    last_error.clear();
    pending_themes.clear();
    pending_settings.clear();
    pending_blocks.clear();
  }

  bool commit() {
    // Validate against copies first. A plugin is an all-or-nothing unit: a
    // duplicate or malformed contribution must not leave half of its themes,
    // settings, or callbacks installed.
    ui::ThemeRegistry candidate_themes = themes;
    ExtensionRegistry candidate_extensions = extensions;
    for (const auto &entry : pending_themes) {
      if (!candidate_themes.registerTheme(entry.id, entry.name, entry.theme,
                                          loading_path, &last_error))
        return false;
    }
    for (const auto &entry : pending_settings) {
      if (!candidate_extensions.registerSetting(entry, &last_error))
        return false;
    }
    for (const auto &entry : pending_blocks) {
      if (!candidate_extensions.registerUiBlock(entry, &last_error))
        return false;
    }

    for (auto &entry : pending_themes) {
      std::string error;
      if (!themes.registerTheme(std::move(entry.id), std::move(entry.name),
                                std::move(entry.theme), loading_path, &error)) {
        last_error = std::move(error);
        return false;
      }
    }
    for (auto &entry : pending_settings) {
      std::string error;
      if (!extensions.registerSetting(std::move(entry), &error)) {
        last_error = std::move(error);
        return false;
      }
    }
    for (auto &entry : pending_blocks) {
      std::string error;
      if (!extensions.registerUiBlock(std::move(entry), &error)) {
        last_error = std::move(error);
        return false;
      }
    }
    return true;
  }

  static int registerTheme(void *context, const char *id, const char *name,
                           const termusic_theme_v1 *theme) {
    auto &self = *static_cast<Bridge *>(context);
    if (!id || !theme) {
      self.last_error = "plugin supplied an invalid theme";
      return 0;
    }
    self.pending_themes.push_back({id, name ? name : id, convertTheme(*theme)});
    return 1;
  }

  static int registerSetting(void *context,
                             const termusic_setting_v1 *setting) {
    auto &self = *static_cast<Bridge *>(context);
    if (!setting || !setting->id || !setting->label) {
      self.last_error = "plugin supplied an invalid setting";
      return 0;
    }
    SettingDescriptor descriptor;
    descriptor.id = setting->id;
    descriptor.section = setting->section ? setting->section : "Extensions";
    descriptor.label = setting->label;
    descriptor.description = setting->description ? setting->description : "";
    descriptor.kind = convertKind(setting->kind);
    descriptor.default_value =
        setting->default_value ? setting->default_value : "";
    descriptor.choices = splitCsv(setting->choices_csv);
    descriptor.minimum = setting->minimum;
    descriptor.maximum = setting->maximum;
    descriptor.restart_required = setting->restart_required != 0;
    self.pending_settings.push_back(std::move(descriptor));
    return 1;
  }

  static int registerUiBlock(void *context, const termusic_ui_block_v1 *block) {
    auto &self = *static_cast<Bridge *>(context);
    if (!block || !block->id || !block->slot || !block->render) {
      self.last_error = "plugin supplied an invalid UI block";
      return 0;
    }
    const auto renderer = block->render;
    void *user_data = block->user_data;
    UiBlock value{block->id, block->slot, block->title ? block->title : "",
                  block->order, [renderer, user_data] {
                    const char *text = renderer(user_data);
                    return text ? std::string(text) : std::string{};
                  }};
    self.pending_blocks.push_back(std::move(value));
    return 1;
  }

  static const char *getSetting(void *context, const char *id,
                                const char *fallback) {
    auto &self = *static_cast<Bridge *>(context);
    if (!id)
      return fallback;
    const std::string full_id(id);
    for (const auto &[plugin_id, values] : self.settings) {
      const std::string prefix = plugin_id + ".";
      if (!full_id.starts_with(prefix))
        continue;
      const auto value = values.find(full_id.substr(prefix.size()));
      if (value != values.end())
        return value->second.c_str();
    }
    return fallback;
  }

  static void log(void *, int level, const char *message) {
    std::cerr << "termusic plugin[" << level
              << "]: " << (message ? message : "") << '\n';
  }
};

PluginManager::PluginManager(
    ExtensionRegistry &extensions, ui::ThemeRegistry &themes,
    const std::map<std::string, std::map<std::string, std::string>> &settings)
    : bridge_(std::make_unique<Bridge>(
          Bridge{extensions, themes, settings, {}, {}, {}, {}, {}})) {
  host_.abi_version = TERMUSIC_PLUGIN_ABI_VERSION;
  host_.host_context = bridge_.get();
  host_.register_theme = &Bridge::registerTheme;
  host_.register_setting = &Bridge::registerSetting;
  host_.register_ui_block = &Bridge::registerUiBlock;
  host_.get_setting = &Bridge::getSetting;
  host_.log = &Bridge::log;
}

PluginManager::~PluginManager() {
  for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
    if (it->descriptor && it->descriptor->shutdown) {
      try {
        it->descriptor->shutdown(it->descriptor->user_data);
      } catch (...) {
        // Destructors must not let a misbehaving C++ plugin terminate the
        // host. The library is still unloaded below.
      }
    }
    if (it->library)
      dlclose(it->library);
  }
}

std::size_t
PluginManager::loadDirectory(const std::filesystem::path &directory) {
  std::error_code error;
  if (!std::filesystem::exists(directory, error))
    return 0;
  std::vector<std::filesystem::path> files;
  for (std::filesystem::directory_iterator it(directory, error), end;
       !error && it != end; it.increment(error)) {
    const auto extension = it->path().extension();
    if (it->is_regular_file() && (extension == ".so" || extension == ".dylib"))
      files.push_back(it->path());
  }
  if (error)
    warnings_.push_back("cannot scan plugin directory: " + error.message());
  std::sort(files.begin(), files.end());
  std::size_t count = 0;
  for (const auto &file : files)
    count += load(file) ? 1U : 0U;
  return count;
}

bool PluginManager::load(const std::filesystem::path &path) {
  bridge_->beginLoad(path);
  void *library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    warnings_.push_back(path.string() + ": " + dlerror());
    return false;
  }
  dlerror();
  auto init = reinterpret_cast<termusic_plugin_init_fn_v1>(
      dlsym(library, TERMUSIC_PLUGIN_ENTRYPOINT));
  if (const char *error = dlerror(); error != nullptr) {
    warnings_.push_back(path.string() + ": missing " +
                        TERMUSIC_PLUGIN_ENTRYPOINT + " (" + error + ")");
    dlclose(library);
    return false;
  }
  const termusic_plugin_v1 *descriptor = nullptr;
  try {
    descriptor = init(&host_);
  } catch (...) {
    warnings_.push_back(path.string() + ": plugin initialization threw");
    dlclose(library);
    return false;
  }
  if (!descriptor || descriptor->abi_version != TERMUSIC_PLUGIN_ABI_VERSION ||
      !descriptor->id || !descriptor->name ||
      !validPluginId(descriptor->id) || *descriptor->name == '\0') {
    warnings_.push_back(path.string() + ": incompatible plugin descriptor");
    dlclose(library);
    return false;
  }
  if (std::any_of(plugin_infos_.begin(), plugin_infos_.end(),
                  [&](const PluginInfo &plugin) {
                    return plugin.id == descriptor->id;
                  })) {
    warnings_.push_back(path.string() + ": duplicate plugin id " +
                        descriptor->id);
    if (descriptor->shutdown) {
      try {
        descriptor->shutdown(descriptor->user_data);
      } catch (...) {
        warnings_.push_back(path.string() + ": plugin shutdown threw");
      }
    }
    dlclose(library);
    return false;
  }
  if (!bridge_->commit()) {
    warnings_.push_back(path.string() + ": " + bridge_->last_error);
    if (descriptor->shutdown) {
      try {
        descriptor->shutdown(descriptor->user_data);
      } catch (...) {
        warnings_.push_back(path.string() + ": plugin shutdown threw");
      }
    }
    dlclose(library);
    return false;
  }
  loaded_.push_back({library, descriptor});
  plugin_infos_.push_back({descriptor->id, descriptor->name,
                           descriptor->version ? descriptor->version : "",
                           path});
  return true;
}

} // namespace termusic::extensions
