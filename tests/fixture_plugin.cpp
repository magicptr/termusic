#include "extensions/plugin_api.h"

#include <string>

namespace {

std::string status;

const char *renderStatus(void *) { return status.c_str(); }

void shutdown(void *) {}

} // namespace

extern "C" const termusic_plugin_v1 *
termusic_plugin_init_v1(const termusic_plugin_host_v1 *host) {
  if (!host || host->abi_version != TERMUSIC_PLUGIN_ABI_VERSION)
    return nullptr;

  termusic_setting_v1 setting{};
  setting.id = "fixture.greeting";
  setting.section = "Fixture";
  setting.label = "Greeting";
  setting.description = "A setting registered across the C ABI";
  setting.kind = TERMUSIC_SETTING_STRING_V1;
  setting.default_value = "hello";
  if (!host->register_setting(host->host_context, &setting))
    return nullptr;
  status = "Greeting: ";
  status += host->get_setting(host->host_context, "fixture.greeting", "hello");

  termusic_theme_v1 theme{};
  if (!host->register_theme(host->host_context, "fixture-dark", "Fixture Dark",
                            &theme))
    return nullptr;

  termusic_ui_block_v1 block{};
  block.id = "fixture.status";
  block.slot = "settings.extensions";
  block.title = "Fixture";
  block.render = &renderStatus;
  if (!host->register_ui_block(host->host_context, &block))
    return nullptr;

  static const termusic_plugin_v1 plugin = {TERMUSIC_PLUGIN_ABI_VERSION,
                                            "fixture",
                                            "Fixture Plugin",
                                            "1.0.0",
                                            nullptr,
                                            &shutdown};
  return &plugin;
}
