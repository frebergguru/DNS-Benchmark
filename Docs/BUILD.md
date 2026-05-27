# Build

## Linux

### Arch / Manjaro

```bash
sudo pacman -S gtk3 glib2 curl openssl cmake gcc pkgconf gettext
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/dnsbenchmark
```

### Debian / Ubuntu

```bash
sudo apt install libgtk-3-dev libglib2.0-dev libcurl4-openssl-dev \
                 libssl-dev cmake gcc pkg-config gettext
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/dnsbenchmark
```

### Fedora

```bash
sudo dnf install gtk3-devel glib2-devel libcurl-devel openssl-devel \
                 cmake gcc pkgconf-pkg-config gettext
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Windows (MSYS2 mingw64)

```bash
pacman -S mingw-w64-x86_64-{gcc,cmake,pkgconf,gtk3,glib2,curl,openssl,gettext}
cmake -S . -B build -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/dnsbenchmark.exe
```

When shipping outside MSYS2, bundle the GTK 3, GLib, libcurl, OpenSSL and
gettext DLLs alongside the executable.

## CMake options

| Option            | Default | Effect                                                     |
|-------------------|---------|------------------------------------------------------------|
| `WITH_DOH`        | `ON`    | DNS-over-HTTPS transport (requires libcurl).               |
| `WITH_DOT`        | `ON`    | DNS-over-TLS transport (requires OpenSSL ≥ 1.1.1).         |
| `WITH_LDNS`       | `OFF`   | Use libldns for full DNSSEC RRSIG validation (otherwise AD-bit only). |
| `BUILD_TESTING`   | `ON`    | Build the test suite.                                      |
| `CMAKE_BUILD_TYPE`| `Release` if unset | `Debug` keeps `assert()` live in the tests. |

Run `cmake -B build -L` after configuring to see all detected variables.

## Tests

```bash
ctest --test-dir build --output-on-failure      # all (incl. live network)
SKIP_NETWORK_TESTS=1 ctest --test-dir build     # offline only (hermetic)
```

The unit tests are forced to keep assertions live even in Release builds
via `-UNDEBUG` on the test targets. See [TESTING.md](TESTING.md).

## Install

```bash
cmake --install build --prefix /usr/local
```

This places:

- `bin/dnsbenchmark`
- `share/dnsbenchmark/resolvers_default.tsv`
- `share/dnsbenchmark/test_domains.txt`
- `share/locale/<lang>/LC_MESSAGES/dnsbenchmark.mo` (one per `po/*.po`)

The binary searches for its data at the `DNSB_DATADIR_BUILD` path embedded
at compile time first (so dev runs from the build tree work without
install), then falls back to `DNSB_DATADIR_INSTALL`.
