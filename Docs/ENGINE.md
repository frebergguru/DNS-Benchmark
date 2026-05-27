# Engine & benchmark methodology

This is what each Run click actually measures.

## Per-resolver query schedule

Each resolver runs in its own worker thread (capped by
`cfg.concurrency`, default 32). The worker does:

1. **Warm-up** — one uncached query for `google.com`. The first response
   is timed and stored in the uncached series; subsequent identical
   queries are expected to hit the resolver's cache.

2. **Query sets** — `cfg.query_sets` iterations of:

   1. **Cached** — query `google.com` again. The resolver should answer
      from cache. Recorded into `r->cached`.
   2. **Uncached** — pick a popular domain from `data/test_domains.txt`,
      prepend a random label, ask for the resulting fresh name. Forces a
      recursive walk. Recorded into `r->uncached`.
   3. **`.com` TLD** — pick a random `<rand>.com`. Tests round-trip to
      the `.com` TLD servers in isolation. Recorded into `r->dotcom`.
   4. Sleep `cfg.spacing_ms` (default 20 ms) before the next set.

3. **Redirection probe** (if `cfg.probe_redirection`) — query
   `<rand>.invalid`. A well-behaved resolver returns `NXDOMAIN`. A
   "redirector" returns a synthetic A record (e.g., an ISP captive-portal
   IP). Detection sets `r->redirects = 1` and turns the status indicator
   orange.

4. **DNSSEC probe** (if `cfg.probe_dnssec`) — query `dnssec-tools.org`
   with the DO bit set. If the response has `AD=1`, the resolver is
   doing signature validation. Sets `r->dnssec_ok = 1`. Off by default
   because some resolvers misbehave when presented with `DO=1` queries.

## Sidelining

A resolver that fails (transport error, parse error, timeout) is
tracked via `consecutive_fails`. When the counter reaches
`cfg.sideline_after` (default 10), `r->sidelined = 1` and the worker
exits early. This stops a dead resolver from eating the rest of the
query budget.

A timed-out or refused response that *does* contain a valid DNS message
(e.g., `REFUSED`, `SERVFAIL`, `NXDOMAIN`) **counts as success** for
timing purposes — what matters is the round-trip, not the rcode. The
old behaviour (treating `NXDOMAIN` as failure) inflated the failure
rate to ~50% on random uncached / `.com` queries.

## Concurrency & coordination

- One `GThreadPool` per run, sized to `cfg.concurrency`.
- Workers don't know about each other; they each iterate their own
  schedule on their own resolver.
- An atomic `e->cancel` flag is checked at every loop boundary so Stop
  is responsive.
- An atomic `e->outstanding` decrements as workers finish; the last one
  emits `DNSB_EVT_RUN_DONE`.

## Event throttling

Workers may emit `DNSB_EVT_PROGRESS` events thousands of times per
second. To avoid drowning the GTK main loop, `emit_event` honours a
100 ms minimum interval per worker, and `on_engine_event` further
coalesces all event kinds through `w->idle_pending` so the main loop
runs at most one `ui_refresh_cb` per idle cycle regardless of how many
workers are firing.

## Stats math

Inside `src/engine/stats.c`:

- `mean = sum / n`
- `stddev = sqrt((sum_sq − n × mean²) / (n − 1))` (sample stddev)
- `median = qsort + middle element / average of middle two`
- `min`, `max` updated incrementally

All three stat fields per resolver (`cached`, `uncached`, `dotcom`) live
under the same per-resolver `stats_mutex` to keep the realloc-able
sample buffer safe against the UI thread's mean/median reads.

## Expected total queries

```
queries_per_resolver = 1 (warmup) + 3 × query_sets
                       + (probe_redirection ? 1 : 0)
                       + (probe_dnssec     ? 1 : 0)
```

The redirection and DNSSEC probes do **not** increment `queries_sent` — so
the progress-bar math in `update_progress_ui` uses `1 + 3 × query_sets`
only. A fully-completed resolver reaches exactly 100 %.

## Tuning knobs

All exposed in the Settings dialog and persisted via `dnsb_engine_set_config`:

| Field              | Default | Meaning                                       |
|--------------------|--------:|-----------------------------------------------|
| `query_sets`       |    50   | Iterations of cached + uncached + dotcom      |
| `spacing_ms`       |    20   | Sleep between sets                            |
| `timeout_ms`       |  1500   | Per-query wall-clock timeout                  |
| `sideline_after`   |    10   | Consecutive failures before sidelining        |
| `concurrency`      |    32   | Max worker threads                            |
| `probe_redirection`|     1   | Run NXDOMAIN-hijack detection                 |
| `probe_dnssec`     |     0   | Run DO/AD probe                               |
