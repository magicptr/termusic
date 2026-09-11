#ifndef TERMUSIC_PLUGIN_API_H
#define TERMUSIC_PLUGIN_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TERMUSIC_PLUGIN_ABI_VERSION 1u
#define TERMUSIC_PLUGIN_ENTRYPOINT "termusic_plugin_init_v1"

typedef struct termusic_rgb_v1 {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} termusic_rgb_v1;

typedef struct termusic_theme_v1 {
  termusic_rgb_v1 background, background_deep, panel;
  termusic_rgb_v1 text, muted_text, weak_text, header_text;
  termusic_rgb_v1 border, border_dim, separator;
  termusic_rgb_v1 accent_primary, accent_secondary, accent_purple;
  termusic_rgb_v1 selected_fg, selected_bg, hover_bg, hover_border;
  termusic_rgb_v1 progress_filled, progress_filled_end, progress_empty;
  termusic_rgb_v1 icon, progress_knob;
  termusic_rgb_v1 spectrum_low, spectrum_high, spectrum_peak;
  termusic_rgb_v1 error, ok;
} termusic_theme_v1;

typedef enum termusic_setting_kind_v1 {
  TERMUSIC_SETTING_BOOL_V1 = 0,
  TERMUSIC_SETTING_INTEGER_V1 = 1,
  TERMUSIC_SETTING_NUMBER_V1 = 2,
  TERMUSIC_SETTING_STRING_V1 = 3,
  TERMUSIC_SETTING_CHOICE_V1 = 4
} termusic_setting_kind_v1;

typedef struct termusic_setting_v1 {
  const char *id;
  const char *section;
  const char *label;
  const char *description;
  termusic_setting_kind_v1 kind;
  const char *default_value;
  const char *choices_csv;
  double minimum;
  double maximum;
  int restart_required;
} termusic_setting_v1;

/// A UI block is intentionally declarative at the ABI boundary. Returning
/// UTF-8 text keeps plugins independent of FTXUI and the C++ standard library.
/// The pointer returned by render must remain valid until the next render call.
typedef const char *(*termusic_render_text_v1)(void *user_data);

typedef struct termusic_ui_block_v1 {
  const char *id;
  const char *slot;
  const char *title;
  int order;
  termusic_render_text_v1 render;
  void *user_data;
} termusic_ui_block_v1;

typedef struct termusic_plugin_host_v1 {
  uint32_t abi_version;
  void *host_context;
  int (*register_theme)(void *host_context, const char *id,
                        const char *display_name,
                        const termusic_theme_v1 *theme);
  int (*register_setting)(void *host_context,
                          const termusic_setting_v1 *setting);
  int (*register_ui_block)(void *host_context,
                           const termusic_ui_block_v1 *block);
  const char *(*get_setting)(void *host_context, const char *id,
                             const char *fallback);
  void (*log)(void *host_context, int level, const char *message);
} termusic_plugin_host_v1;

typedef struct termusic_plugin_v1 {
  uint32_t abi_version;
  const char *id;
  const char *name;
  const char *version;
  void *user_data;
  void (*shutdown)(void *user_data);
} termusic_plugin_v1;

typedef const termusic_plugin_v1 *(*termusic_plugin_init_fn_v1)(
    const termusic_plugin_host_v1 *host);

/// Every plugin exports this function with C linkage:
/// const termusic_plugin_v1 *termusic_plugin_init_v1(
///     const termusic_plugin_host_v1 *host);

#ifdef __cplusplus
}
#endif

#endif
