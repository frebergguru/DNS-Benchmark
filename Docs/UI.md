# UI

## Window structure

```
GtkApplicationWindow (w->root)
└── vbox
    ├── GtkHeaderBar  (packed inline, NOT used as CSD titlebar)
    │     ├── start: [Run] [Stop]
    │     └── end:   [Add] [Clone] [Save PNG] [Save CSV] [Settings] [🌐 lang] [◐ theme]
    ├── GtkProgressBar  (progress + ETA text)
    └── GtkNotebook  (4 tabs)
        ├── Introduction       (welcome markup label)
        ├── Nameservers        (chart + legend)
        ├── Tabular Data       (sortable GtkTreeView)
        └── Conclusions        (rendered after each run)
```

The header bar is intentionally a regular widget, not the GtkWindow's
CSD titlebar. KDE Plasma + Breeze paints its `.titlebutton` icons as
layered two-tone glyphs that CSS recolor can't override cleanly, so we
let the WM draw native window decorations and put our toolbar below.

## Shared list store

Both the chart and the Tabular Data tab read from one
`GtkListStore` (`w->store`) that mirrors the engine's resolver array.
Columns are defined in `src/ui/model_cols.h`.

- The **Tabular** tab wraps the store in a `GtkTreeModelSort` so the
  user can re-sort by any column.
- The **chart** wraps the store in a separate `GtkTreeModelSort` fixed
  on `DNSB_COL_CACHED_MS` ascending, so the chart always shows fastest-
  first regardless of how the user sorted the table.
- Both sort models use `dnsb_pin_first_compare`, which puts pinned rows
  at the top of every column.

## Drawing the chart

`src/ui/chart.c` is a `GtkDrawingArea` with a custom `draw` handler.
It iterates the bound (sorted) model in order and emits, per row, the
status dot + name/address labels + three horizontal bars (cached /
uncached / .com). Colors come from `dnsb_theme_colors_current()` so a
theme switch lives in one place.

The chart connects to model `row-changed` / `row-inserted` /
`row-deleted` / `rows-reordered` signals to redraw automatically when
the underlying store updates. **It also unregisters from the theme
listener list and disconnects model signal handlers in its `destroy`
handler** — both pre-existing UAF traps before the cleanup pass.

## Theme system

[See THEME.md.](THEME.md)

Two embedded GtkCssProvider stylesheets, one dark and one light, with a
palette taken verbatim from https://www.freberg.guru. Cairo bar colors
live in a parallel `dnsb_theme_colors` struct so the chart matches the
chrome.

The `◐` button cycles `auto → dark → light → auto`, persisting the
choice to `~/.config/DNS-Benchmark/theme`. Listeners (e.g., the chart)
register via `dnsb_theme_register_listener` and **must unregister
during their destroy** or the next theme change will fire callbacks on
freed memory.

## i18n

[See I18N.md.](I18N.md)

Everything user-visible goes through `_()` (gettext). The `🌐` popover
lets the user switch between Auto (system locale), English, and Norwegian
Bokmål. Switching applies in-place — `dnsb_i18n_set_language` bumps
glibc's `_nl_msg_cat_cntr` to invalidate the gettext cache, then
`dnsb_app_rebuild_window` swaps the window for a freshly-translated one
while the engine pointer (and all recorded stats) persists.

## Progress bar

`update_progress_ui` in `window.c` is called from `ui_refresh_cb` (which
runs throttled at ~10 Hz). It:

1. Computes per-resolver completion from `queries_sent / expected_per`
   under `r->stats_mutex`.
2. Averages across resolvers (sidelined → 1.0).
3. After `≥ 3 s` of elapsed time **and** `≥ 5 %` progress, shows an ETA
   computed as `elapsed × (1 − p) / p` and smoothed with an EMA
   (`smoothed = 0.7 × prev + 0.3 × raw`) to damp the spikes when a
   batch of resolvers finishes at once.

Before that threshold the text reads `12.3 %  4/30 resolvers ·
estimating…`. After completion: `Done in 47s`.

## Status indicators

The chart's status dot is filled if the resolver is currently
configured on the system (read at startup via
`dnsb_get_system_resolvers`), hollow otherwise. Color encodes state:

| Status code (`DNSB_COL_STATUS`) | Dot color   | Meaning                       |
|--------------------------------:|-------------|-------------------------------|
| `0`                             | dim         | idle (no queries yet)         |
| `1`                             | green       | healthy                       |
| `2`                             | red         | unresponsive                  |
| `3`                             | orange      | redirector (NXDOMAIN hijack)  |
| `-1`                            | gray        | sidelined (10 consecutive fails) |

A pinned resolver gets a yellow outer ring around the dot.

## Page cloning

The Clone toolbar button deep-copies the current list store, opens a
new top-level window with the Nameservers + Tabular Data tabs bound to
the snapshot, and unrefs the snapshot store (the widgets keep it alive
via ref-counting). The clone freezes — it does not update with the
ongoing benchmark.

## Custom dialogs

Settings, Add Resolver, file pickers — all built imperatively inline in
`window.c`. Translation strings live in `po/DNS-Benchmark.pot`.
