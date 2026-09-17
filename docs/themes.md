# Custom themes

Termusic loads optional theme files from the `themes` directory next to
`config.toml`:

```text
~/.config/termusic/
├── config.toml
└── themes/*.toml
```

The directory can be overridden with `appearance.theme_directory` in
`config.toml` or with `--theme-dir` for one invocation.

A theme inherits every omitted colour from the default theme, so a small theme
remains forward compatible when the application adds new colour roles:

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
changed immediately under Core → Appearance, and the selected stable ID is
saved in the configuration.
