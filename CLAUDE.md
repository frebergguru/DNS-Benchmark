# CLAUDE.md

Project memory for Claude Code. Read at the start of every session.

## What this is

A from-scratch cross-platform DNS benchmarking tool in GTK 3 / C
(Linux + Windows MSYS2). Times cached / uncached / .com lookups across
many resolvers in parallel, supports UDP / TCP / DoH / DoT, sortable
table + Cairo bar chart, dark/light "CRT amber" themes inspired by
https://www.freberg.guru, English + Norwegian UI.

Full architecture in [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md). The
plan that drove the initial six milestones lives in
`/home/hypnotize/.claude/plans/please-make-a-dns-mellow-fairy.md`.

## Quick commands

```bash
cmake -S . -B build && cmake --build build -j           # build
ctest --test-dir build --output-on-failure              # all tests (incl. live)
SKIP_NETWORK_TESTS=1 ctest --test-dir build             # offline only
./build/dnsbenchmark                                    # run
```

Regenerate i18n template after adding `_()` strings:

```bash
xgettext --keyword=_ --keyword=N_ --from-code=UTF-8 \
  --output=po/dnsbenchmark.pot $(find src -name '*.c')
msgmerge --update po/nb.po po/dnsbenchmark.pot
```

## Threading invariants — do not break

- **Never touch a GTK widget from a worker thread.** Workers post via
  `g_idle_add(ui_refresh_cb, w)` only.
- Every read or write of resolver stats fields (`queries_sent`,
  `queries_ok`, `consecutive_fails`, `sidelined`, `redirects`,
  `dnssec_ok`, and the three `dnsb_stats` buffers) must hold
  `r->stats_mutex`. Workers and the UI both touch these; concurrent
  `realloc` (in `dnsb_stats_add`) vs `qsort` (in `dnsb_stats_median`)
  was a real crash before this lock existed.
- `dnsb_engine_set_callback` and `emit_event` both take
  `e->callback_mutex`. Don't drop it from either side.
- `dnsb_engine_set_domains` and worker reads of `uncached_pool` both
  take `e->mutex`.
- `OPENSSL_init_ssl` is one-shot through `g_once_init_enter` in
  `transport_dot.c` — never replace it with a plain `static int` flag.

## Cleanup invariants — also do not break

- Widgets that register a theme listener (`dnsb_theme_register_listener`)
  **must** call `dnsb_theme_unregister_listener` in their destroy
  handler. The chart does this — see `on_chart_destroyed`.
- `dnsb_window_destroy` clears the engine callback, drains
  `g_idle_remove_by_data(w)`, and only calls `gtk_widget_destroy(w->root)`
  if the weak pointer hasn't already auto-nulled the field. Keep these
  three steps in this order.
- `w->root` carries a `g_object_add_weak_pointer` so it auto-nulls when
  GTK destroys the window during `g_application_run`. Don't drop that
  weak ref.

## Cross-platform quirks

- **Windows CSD on KDE / Breeze**: we deliberately do **not** call
  `gtk_window_set_titlebar()`. The header bar widget is packed inline
  as a normal vbox child and the WM draws its native decorations.
  Breeze's `.titlebutton` icons use multi-layer `-gtk-icon-source` that
  CSS recolor can't override cleanly, and three half-styled window
  controls look worse than zero half-styled ones.
- **Windows time**: `dnsb_now_ns` splits the QPC multiplication as
  `(qpc / freq) * 1e9 + (qpc % freq) * 1e9 / freq` to avoid uint64
  overflow after ~15 min on a 10 MHz QPC. Don't simplify back to
  `qpc * 1e9 / freq`.
- **Windows sockets**: `SOCKET` is `ULONG_PTR`, wider than `int`.
  `dnsb_sock` is the canonical handle type; do not cast it to `int`
  except inside `ssl_set_native_fd` (the one place forced by OpenSSL's
  `SSL_set_fd` prototype, where we use `(int)(intptr_t)fd`).
- **i18n cache invalidation**: `_nl_msg_cat_cntr` is a glibc / GNU
  gettext internal counter that gettext checks per call. Bumping it
  forces re-read on the next `_("…")`. It exists on MSYS2 mingw64 too,
  but `transport_dot.c`'s feature test must stay `#if defined(__GLIBC__)
  || defined(__GNU_LIBRARY__)` so the link is graceful on MUSL / BSD.

## Where things live

| What                              | File                                  |
|-----------------------------------|---------------------------------------|
| App entry, gettext init           | `src/main.c`                          |
| Window, header bar, dialogs       | `src/ui/window.c`                     |
| Cairo bar chart                   | `src/ui/chart.c`                      |
| Theme palettes + GTK CSS          | `src/ui/theme.c`                      |
| Tabular tab, sort comparator      | `src/ui/tab_tabular.c`                |
| Engine + worker pool              | `src/engine/engine.c`                 |
| Stats (mean/median/stddev)        | `src/engine/stats.c`                  |
| RFC 1035 codec                    | `src/dns/packet.c`                    |
| UDP/TCP/DoH/DoT transports        | `src/dns/transport_{udp,tcp,doh,dot}.c` |
| Resolver TSV loader               | `src/io/resolvers_io.c`               |
| CSV export                        | `src/io/csv_export.c`                 |
| System resolver detection         | `src/platform/sysresolvers_{linux,win}.c` |
| Monotonic clock + socket compat   | `src/net/{time_ns,socket_compat}.c`   |
| i18n macros + lang preference     | `src/util/i18n.c`                     |
| Norwegian Bokmål translation      | `po/nb.po`                            |

## Conventions

- **Status codes** in `DNSB_COL_STATUS`: `-1` sidelined · `0` idle · `1`
  ok · `2` fail · `3` redirector.
- **RTTs** are stored in nanoseconds (`uint64_t`, from `dnsb_now_ns`)
  and only converted to ms (`dnsb_ns_to_ms`) at the display / stats
  boundary.
- **Strings** to translate go through `_()`. Don't string-concat
  pre- and post-translation; let the whole sentence be one msgid.
- **Comments**: usually none. Add one only when the *why* is
  non-obvious (a hidden invariant, a workaround for a specific bug).
  The codebase has plenty of "fixed X because Y happened" comments at
  the lock acquisition sites — leave them; they're the load-bearing
  documentation of the mutex contract.
- **Tests**: any new test must be added via `dnsb_add_test()` in
  `tests/CMakeLists.txt` (it adds `-UNDEBUG` so `assert()` runs in
  Release). Smoke/network tests use `SKIP_RETURN_CODE 77` and gate on
  `SKIP_NETWORK_TESTS=1`.

## Pitfalls already paid for in this codebase

These bugs were found, diagnosed, fixed. Don't reintroduce.

- ❌ Treating `NXDOMAIN` / `REFUSED` rcodes as transport failures —
  inflates failure rate. A wire response is a wire response.
- ❌ Per-event `g_idle_add(ui_refresh_cb, w)` without throttling —
  drowns the main loop. Use the `idle_pending` atomic.
- ❌ Calling `gtk_widget_destroy(w->root)` after `g_application_run`
  returns — the widget is already gone. Weak pointer.
- ❌ Mutating engine config / domain pool without `e->mutex`.
- ❌ Forgetting to unregister theme listeners on chart destroy.
- ❌ Linear ETA from `t = 0` with no minimum thresholds — gives wild
  early estimates. Wait for `elapsed ≥ 3 s` and `progress ≥ 5 %`, then
  EMA-smooth.
- ❌ Spinbutton `+`/`−` arrows looking blurry — caused by the catch-all
  `button { padding: 4px 12px }`. Override `spinbutton button` padding
  to `0 4px`.
- ❌ Inherited `-gtk-icon-shadow` from base theme. Kill globally with
  `* { -gtk-icon-shadow: none; text-shadow: none; }`.

## Docs index

- [Architecture](Docs/ARCHITECTURE.md) — modules + threading + lifecycle
- [Build](Docs/BUILD.md) — Linux / Windows / Fedora steps
- [Engine](Docs/ENGINE.md) — benchmark methodology + tuning knobs
- [UI](Docs/UI.md) — widget tree, chart, progress bar
- [Theme](Docs/THEME.md) — palette + CSS quirks
- [I18N](Docs/I18N.md) — gettext workflow + adding languages
- [Testing](Docs/TESTING.md) — suite layout + writing new tests
