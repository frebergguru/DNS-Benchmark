# DNS Benchmark

A cross-platform DNS benchmarking tool.
Written in C with a GTK 3 GUI. Runs on Linux and Windows.

Times **cached**, **uncached**, and **.com** lookups against many resolvers
in parallel, detects domain-redirection (NXDOMAIN hijacking), probes DNSSEC,
and ranks the results with a sortable table and a bar chart.

## Features

- Plain DNS over UDP and TCP (IPv4 + IPv6)
- DNS-over-HTTPS (DoH) via libcurl HTTP/2
- DNS-over-TLS (DoT) via OpenSSL with TLS-session caching
- Concurrent benchmarking (configurable worker pool, default 32)
- Sortable per-column table; pin resolvers to the top
- Live-updating bar chart (red = cached, green = uncached, purple = .com)
- Status dots: filled = system-configured, hollow = available, color =
  ok/fail/redirect/sidelined
- Redirection (NXDOMAIN-hijack) detection
- Optional DNSSEC probe (AD-bit check on a known signed name)
- Save CSV results
- Save chart as PNG
- Clone Page (snapshot a frozen view in a new window)
- Detects currently-configured system resolvers (`/etc/resolv.conf` on Linux,
  `GetAdaptersAddresses` on Windows)
- Built-in **dark** and **light** themes (CRT-amber palette inspired by
  https://www.freberg.guru), cycle via the `◐` button in the header bar.
  Preference is persisted to `~/.config/dnsbenchmark/theme`.
- Internationalized — **English** (source) and **Norwegian Bokmål** included.
  Follows the system locale by default; override at launch with
  `LANG=nb_NO.UTF-8 ./dnsbenchmark` or `LANG=en_US.UTF-8 ./dnsbenchmark`.
  Add a new language by dropping `po/<lang>.po` into the project and
  rebuilding — CMake compiles it to `.mo` automatically.

## Download (pre-built)

Grab the latest `DNS-Benchmark-x86_64.AppImage` from
[Releases](https://github.com/frebergguru/DNS-Benchmark/releases), then:

```bash
chmod +x DNS-Benchmark-x86_64.AppImage
./DNS-Benchmark-x86_64.AppImage
```

Runs on most glibc-based Linux distros — GTK 3 and every transitive
library are bundled inside. No install step needed.

## Build (Linux)

```bash
# Arch / Manjaro
sudo pacman -S gtk3 glib2 curl openssl cmake gcc pkgconf

# Debian / Ubuntu
sudo apt install libgtk-3-dev libglib2.0-dev libcurl4-openssl-dev libssl-dev \
                 cmake gcc pkg-config

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/dnsbenchmark
```

Run the test suite:

```bash
ctest --test-dir build --output-on-failure                # all tests
SKIP_NETWORK_TESTS=1 ctest --test-dir build               # offline only
```

## Build (Windows, MSYS2 mingw64)

```bash
pacman -S mingw-w64-x86_64-{gcc,cmake,pkgconf,gtk3,glib2,curl,openssl}
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/dnsbenchmark.exe
```

Bundle GTK/GLib/libcurl/OpenSSL DLLs alongside the executable when shipping
outside of an MSYS2 shell.

## CMake options

| Option        | Default | Effect |
|---------------|---------|--------|
| `WITH_DOH`    | `ON`    | Build DNS-over-HTTPS transport (requires libcurl). |
| `WITH_DOT`    | `ON`    | Build DNS-over-TLS transport (requires OpenSSL).   |
| `WITH_LDNS`   | `OFF`   | Use libldns for full DNSSEC RRSIG validation.       |
| `BUILD_TESTING` | `ON`  | Build the test suite.                              |

## Adding resolvers

Edit `data/resolvers_default.tsv`. Format (tab-separated):

```
name<TAB>owner<TAB>address<TAB>transport<TAB>port<TAB>hostname
```

- `transport` is one of `udp`, `tcp`, `doh`, `dot`.
- `hostname` is optional; required only for DoH/DoT when the cert's SAN does
  not cover the literal IP (e.g. Google: `dns.google`).
- Lines starting with `#` are comments.

Or click **Add** in the toolbar to add one at runtime.

## Project layout

```
src/
  main.c                # GtkApplication entry
  ui/                   # window, tabs, chart, dialogs
  engine/               # benchmark orchestrator + stats
  dns/                  # packet codec + UDP/TCP/DoH/DoT transports
  net/                  # cross-platform sockets, monotonic clock
  io/                   # TSV loader, CSV export
  platform/             # system-resolver detection per OS
  util/                 # log, strings
data/
  resolvers_default.tsv # built-in resolver list
  test_domains.txt      # popular domains used to force recursion
tests/
  test_*.c              # offline unit tests
  smoke_*.c             # live integration tests (skipped via SKIP_NETWORK_TESTS=1)
```

## Methodology

For each resolver, the engine runs N **query-sets** (default 50; configurable
1×–100×). Each set contains three measurements:

| Bar       | Color  | Query                                                     |
|-----------|--------|-----------------------------------------------------------|
| Cached    | red    | A popular pre-warmed domain — exercises the resolver cache. |
| Uncached  | green  | A random subdomain of a popular domain — forces recursion. |
| .com TLD  | purple | A random `<rand>.com` — isolates round-trip to the `.com` TLD. |

After the per-set loop, each resolver is optionally probed for:

- **Redirection**: a random `<rand>.invalid` query. Well-behaved resolvers
  return `NXDOMAIN`; redirectors return a synthetic A record (flagged orange).
- **DNSSEC**: an A query for `dnssec-tools.org` with the DO bit set. The AD
  flag in the response indicates the resolver validates signatures.

After 10 consecutive failures a resolver is **sidelined** to avoid wasting
the rest of the budget.

## License

(specify your license here — none chosen yet)
