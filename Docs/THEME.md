# Theme system

Two built-in themes (dark + light) inspired by https://www.freberg.guru,
a CRT-amber terminal aesthetic.

## Palette

| Token            | Dark        | Light        | Used for                                |
|------------------|-------------|--------------|-----------------------------------------|
| `bg`             | `#000000`   | `#F2E5C7`    | window / dialog background              |
| `fg`             | `#FFBF00`   | `#5A3A00`    | primary text, primary accent (Run)      |
| `fg_bright`      | `#FFE066`   | `#3D2700`    | selected tab text, focus border         |
| `fg_dim`         | `#B07F00`   | `#8A6520`    | inactive borders, scrollbar thumb       |
| `fg_faint`       | 6 % `fg`    | 7 % `fg`     | row tints, "empty bar" baseline         |

These are mirrored verbatim from `freberg.css`. Source:
`src/ui/theme.c`.

Bar-chart colors are themed independently so the three lookup classes
stay perceptually distinct on both backgrounds:

| Layer            | Dark      | Light     |
|------------------|-----------|-----------|
| cached (red)     | amber `#FFBF00` | brick `#9F3819` |
| uncached (green) | CRT green `#6BFF6B` | moss `#2E6B2E` |
| .com (purple)    | CRT magenta `#FF7AFF` | aubergine `#7D3893` |

## Cycle

The `◐` button cycles `auto → dark → light → auto`. Persisted to
`~/.config/dnsbenchmark/theme`. In `auto`, dark is chosen unless
`GTK_THEME` contains `dark` or `DNSB_PREFER_DARK=1` is set in the env.

## How widgets pick up the theme

- Standard GTK widgets get themed via a `GtkCssProvider` loaded at
  priority `GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 100`. The CSS is
  inlined as one big string per variant in `theme.c` and rebuilt on
  every change.
- Custom Cairo widgets (the chart, the status dot inside the chart)
  pull from `dnsb_theme_colors_current()` and redraw on theme change
  via the listener registry.

## Listener registry

```c
dnsb_theme_register_listener(on_theme_changed, my_data);
// ...
dnsb_theme_unregister_listener(on_theme_changed, my_data);  // mandatory!
```

When a widget that registered is destroyed, it **must** unregister, or
the next theme change will deref a freed pointer. The chart does this
in its `destroy` signal handler — see `on_chart_destroyed` in
`src/ui/chart.c`. If you add another custom-drawn widget that consumes
theme colors, follow the same pattern.

## Notes on GTK CSS quirks we hit

1. The global `label { color: @fg; }` rule overrides the inherited
   `color` on labels inside buttons. We force it back with
   `button label, button image { color: inherit; }`.
2. Inherited `-gtk-icon-shadow` from the base theme caused doubled
   "ghost" icons. Killed globally with `* { -gtk-icon-shadow: none;
   text-shadow: none; }`.
3. The catch-all `button { padding: 4px 12px }` was making spin-button
   `+`/`−` arrow children oversized; we override with explicit
   `spinbutton button { padding: 0 4px; min-width: 22px; }`.
4. Breeze's `.titlebutton` icons use multi-layer `-gtk-icon-source`
   that we couldn't recolor cleanly. We avoid GTK CSD entirely (see
   [UI.md](UI.md)) so this stops mattering.
