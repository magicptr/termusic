# Themes and plugins

Termusic discovers optional extensions beside its configuration file by
default:

```text
~/.config/termusic/
├── config.toml
├── themes/*.toml
└── plugins/*.so
```

Both directories can be overridden in `config.toml` or for one invocation with
`--theme-dir` and `--plugin-dir`. Use `--no-plugins` to disable native code and
`--list-plugins` to diagnose discovery without starting the UI.

## Theme files

A theme inherits every omitted colour from `default`, so a small theme remains
forward compatible when the core adds new colour roles:

```toml
[theme]
id = "forest"
name = "Forest"

[colors]
background = "#101713"
panel = "#16211b"
text = "#e4eee7"
muted_text = "#9caf9f"
border = "#648c6f"
accent_primary = "#8fce9d"
accent_secondary = "#e0b86b"
selected_fg = "#101713"
selected_bg = "#8fce9d"
```

Every field in `ui::Theme` is accepted in `[colors]`. A malformed colour or a
duplicate theme ID is reported as a non-fatal startup warning. Themes can be
changed immediately from Settings → Theme and the selected stable ID is saved.

## Native plugin ABI

Plugins include the installed `termusic/plugin_api.h` (or
`src/extensions/plugin_api.h` in this repository) and export one C symbol:

```cpp
extern "C" const termusic_plugin_v1 *
termusic_plugin_init_v1(const termusic_plugin_host_v1 *host);
```

The descriptor and strings it references must remain valid until unload.
Plugins may register:

- palettes, which become normal selectable themes;
- setting descriptors, whose values live under `[plugin.<plugin-id>]`;
- declarative text UI blocks in named slots. The first supported slot is
  `settings.extensions`.

The text boundary is deliberate: plugins do not link against FTXUI or expose
C++ standard-library types, which keeps the ABI small and versionable. The
host keeps the shared object loaded while any registered callback is reachable
and invokes `shutdown` before unload.

A minimal build command is:

```bash
c++ -std=c++20 -fPIC -shared my_plugin.cpp -I/path/to/include \
  -o ~/.config/termusic/plugins/my_plugin.so
```

Native plugins execute in-process with the same permissions as Termusic. Only
install plugins you trust.

## Adding more UI and settings

Core UI modules should depend on `ExtensionRegistry` and consume a named slot;
they should not teach the plugin manager about FTXUI. New settings should be
described with `SettingDescriptor`, while persistence remains generic. This
keeps discovery/loading, configuration schema and rendering independent.
