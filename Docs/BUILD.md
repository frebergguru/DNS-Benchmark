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
- `share/applications/dnsbenchmark.desktop`
- `share/icons/hicolor/scalable/apps/dnsbenchmark.svg`
- `share/locale/<lang>/LC_MESSAGES/dnsbenchmark.mo` (one per `po/*.po`)

The binary searches for its data at the `DNSB_DATADIR_BUILD` path embedded
at compile time first (so dev runs from the build tree work without
install), then falls back to `DNSB_DATADIR_INSTALL`.

## Self-contained release builds (AppImage)

For the **GitHub Releases** page we ship an `AppImage` — a single
executable file that bundles GTK 3 and every transitive `.so` so users
can `chmod +x DNS-Benchmark-x86_64.AppImage && ./DNS-Benchmark-x86_64.AppImage`
on most glibc-based Linux distros without installing anything.

```bash
./scripts/build-appimage.sh                       # native arch
ARCH=aarch64 ./scripts/build-appimage.sh          # ARM64
ARCH=x86_64  ./scripts/build-appimage.sh          # explicit x86_64
# or, from a configured build dir, native-only:
cmake --build build --target appimage
```

The first run downloads `linuxdeploy` + the GTK plugin into
`.appimage-tools/` (cached for subsequent runs). Output lands in the
project root as `DNS-Benchmark-<arch>.AppImage`.

### Cross-arch builds (e.g. ARM64 from an x86_64 host)

When `ARCH` differs from `$(uname -m)`, the script transparently
re-launches itself inside a same-architecture Ubuntu Docker container
under qemu-user. One-time host setup:

```bash
# Make sure binfmt_misc is registered for qemu-aarch64 (and other arches):
sudo docker run --privileged --rm tonistiigi/binfmt --install all
```

After that, `ARCH=aarch64 ./scripts/build-appimage.sh` produces a
working `DNS-Benchmark-aarch64.AppImage` even on an x86_64 build host.
The cross-build is slower (~3–5× emulation overhead) but fully
self-contained — no cross-compiler or aarch64 sysroot required on the
host.

If you have a real ARM64 machine (Raspberry Pi 4+, Apple Silicon via
Linux VM, ARM cloud instance, etc.), running the script there natively
is faster and avoids the qemu dependency entirely.

### Why AppImage instead of "fully static"

Truly statically linking GTK 3 on Linux isn't realistic in practice:

- Distro packages of GTK 3 do not ship `.a` archives — only `.so`.
- gdk-pixbuf image loaders, GTK input methods, and a11y bridges are
  loaded with `dlopen` at runtime; even if GTK itself were static, an
  app linked against a static GTK would silently lose image rendering,
  international input, and accessibility.
- Theme engines, icon themes, and locale data live in `$XDG_DATA_DIRS`
  and can't be embedded in a single ELF.

AppImage produces the same *user-facing* outcome (one self-contained
file) by bundling the dynamic stack alongside the binary, and is the
de-facto standard for desktop-app releases on Linux.

### Releasing on GitHub

1. Tag the release: `git tag v0.1.0 && git push --tags`
2. Build: `./scripts/build-appimage.sh`
3. On the GitHub Releases page, attach the resulting
   `DNS-Benchmark-x86_64.AppImage` to the new tag.

Optional: also attach the SHA-256 (`sha256sum DNS-Benchmark-*.AppImage`)
so users can verify their download.
