# Architecture

A short tour of how the codebase fits together. Read this first.

## High-level picture

```
                       ┌────────────────────┐
                       │   GtkApplication   │  main loop
                       └─────────┬──────────┘
                                 │
                       ┌─────────▼──────────┐
                       │   dnsb_window      │  UI: header bar + notebook
                       │  (src/ui/window.c) │  4 tabs + chart + dialogs
                       └─────────┬──────────┘
                                 │  on_engine_event (worker → UI bridge)
                                 │  via g_idle_add(ui_refresh_cb, w)
                                 ▼
                       ┌────────────────────┐
                       │   dnsb_engine      │  benchmark orchestrator
                       │ (src/engine/      ) │
                       └─────────┬──────────┘
                                 │ GThreadPool (one worker per resolver,
                                 │              capped by cfg.concurrency)
                                 ▼
                       ┌────────────────────┐
                       │  do_one_query()    │  per-resolver worker_func
                       └─────────┬──────────┘
                                 │ dispatches by r->transport
        ┌────────────────────────┼────────────────────────┐
        ▼                        ▼                        ▼
   ┌─────────┐             ┌──────────┐             ┌──────────┐
   │  UDP    │             │   TCP    │             │ DoH/DoT  │
   │transport│             │transport │             │transport │
   │  _udp.c │             │  _tcp.c  │             │ _doh/_dot│
   └─────────┘             └──────────┘             └──────────┘
        │                        │                        │
        └────────────────────────┴────────────────────────┘
                                 │  dnsb_pkt_build_query  /
                                 │  dnsb_pkt_parse_response
                                 ▼
                       ┌────────────────────┐
                       │  dns/packet.c      │  RFC 1035 codec
                       └────────────────────┘
```

## Threading model

| Thread                | Owns / drives                         | Touches                                 |
|-----------------------|---------------------------------------|------------------------------------------|
| Main (GTK)            | All widgets, list store, chart redraw | Reads engine stats under `r->stats_mutex`|
| Worker threads (pool) | DNS queries, transport state          | Writes resolver stats under `r->stats_mutex`; signals UI via `dnsb_engine_set_callback` → `on_engine_event` → `g_idle_add` |

The main rule: **never touch a GTK widget from a worker.** Engine callbacks
post events via `g_idle_add(ui_refresh_cb, w)`, which always runs on the
main thread.

## Locking

| Mutex                              | Protects                                              | Held by               |
|------------------------------------|-------------------------------------------------------|-----------------------|
| `e->mutex`                         | `e->cfg`, `e->uncached_pool`                          | `set_config`, `get_config`, `set_domains`, worker before reading `uncached_pool` |
| `e->callback_mutex`                | `e->cb`, `e->cb_user` (so workers can't see a torn pair) | `set_callback`, `emit_event` |
| `r->stats_mutex` (per-resolver)    | `r->cached/uncached/dotcom` stats buffers, `queries_sent`, `queries_ok`, `consecutive_fails`, `sidelined`, `redirects`, `dnssec_ok` | Worker `do_one_query` epilogue; UI `refresh_row`, `update_progress_ui`, `build_conclusion_text`, `dnsb_csv_export` |
| GLib internal (`g_idle_add`)       | Idle source queue                                     | All threads           |

`atomic_int e->cancel`, `e->running`, `e->outstanding` are lock-free
counters/flags.

`atomic_int w->idle_pending` coalesces engine events so a burst from many
workers turns into a single UI refresh.

## Lifecycle

1. **Startup** (`main.c`): create `dnsb_engine`, init theme, init i18n,
   load resolver TSV, detect system resolvers, create `dnsb_window`.
2. **Run** (`on_run_clicked`): reset engine stats, set `running=1`, push N
   resolvers into the thread pool. Workers stream events back.
3. **Stop** (`on_stop_clicked` or engine completion): `cancel=1`, drain
   pool. `running=0`. Conclusion text generated. Save button re-enabled.
4. **Window rebuild** (`dnsb_app_rebuild_window`, language switch): stop
   engine, create a fresh `dnsb_window` with the same engine pointer, then
   destroy the old window. Engine state survives the swap.
5. **Shutdown** (`g_application_run` returns): window already destroyed
   by GTK; `w->root` is auto-nulled by the weak pointer set in
   `dnsb_window_new`. `dnsb_engine_free` joins any straggling workers.

## Where things live

| What                              | File                              |
|-----------------------------------|-----------------------------------|
| GTK entry, gettext init           | `src/main.c`                      |
| Window + dialogs + header bar     | `src/ui/window.c`                 |
| Bar chart (Cairo)                 | `src/ui/chart.c`                  |
| Theme palettes + CSS              | `src/ui/theme.c`                  |
| Sortable table                    | `src/ui/tab_tabular.c`            |
| Engine orchestrator               | `src/engine/engine.c`             |
| Stats (mean/median/stddev)        | `src/engine/stats.c`              |
| Wire format codec                 | `src/dns/packet.c`                |
| UDP / TCP / DoH / DoT transports  | `src/dns/transport_{udp,tcp,doh,dot}.c` |
| Resolver-list TSV loader          | `src/io/resolvers_io.c`           |
| CSV export                        | `src/io/csv_export.c`             |
| System resolver detection         | `src/platform/sysresolvers_*.c`   |
| Monotonic high-res clock          | `src/net/time_ns.c`               |
| Win32/POSIX socket shim           | `src/net/socket_compat.c`         |
| i18n macros + lang preference     | `src/util/i18n.c`                 |

## Conventions

- **Status codes** (in the list store, `DNSB_COL_STATUS`):
  `-1` sidelined, `0` idle, `1` ok, `2` fail, `3` redirector.
- **Transport enum**: `UDP=0, TCP=1, DOH=2, DOT=3`.
- **Time units**: all RTTs are stored in nanoseconds internally
  (`dnsb_now_ns`) and converted to milliseconds (`dnsb_ns_to_ms`) only at
  display / stats-recording time.
- **String marking**: every user-visible string goes through `_()`
  (gettext); see [I18N.md](I18N.md).
- **No GTK from workers.** No exceptions.
