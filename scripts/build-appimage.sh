#!/usr/bin/env bash
#
# Build a self-contained DNS-Benchmark-<arch>.AppImage from the current
# source tree. This is the recommended release artefact for the GitHub
# Releases page — see Docs/BUILD.md for the rationale.
#
# Usage:
#   ./scripts/build-appimage.sh               # native: builds for $(uname -m)
#   ARCH=x86_64  ./scripts/build-appimage.sh  # explicit x86_64
#   ARCH=aarch64 ./scripts/build-appimage.sh  # ARM64 — native if host is
#                                             # aarch64, else uses Docker
#                                             # + qemu-user (see below).
#
# Native build host deps:
#   cmake, gcc, pkg-config, GTK 3 + GLib + libcurl + OpenSSL headers,
#   gettext, file, wget (first run, to fetch linuxdeploy)
#
# Cross-arch (e.g. aarch64 from an x86_64 host) deps:
#   docker (daemon running), qemu-user-static, binfmt_misc registration
#   for qemu-aarch64. Quick one-time setup on most distros:
#     sudo docker run --privileged --rm tonistiigi/binfmt --install all
#   You only need this once per host.
#
# Output: ./DNS-Benchmark-<arch>.AppImage in the project root.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
HOST_ARCH="$(uname -m)"
ARCH="${ARCH:-$HOST_ARCH}"

cd "$PROJECT_DIR"

# ----------------------------------------------------------------------
# Cross-arch path: re-exec inside a same-arch Ubuntu container running
# under qemu-user. The script then takes the native path on the second
# pass because uname -m inside the container matches $ARCH.
# ----------------------------------------------------------------------
if [[ "$ARCH" != "$HOST_ARCH" && "${DNSB_IN_CROSS_CONTAINER:-0}" != "1" ]]; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "ARCH=$ARCH but host is $HOST_ARCH and docker is not installed." >&2
        echo "Install docker + qemu-user-static, or run this script on an $ARCH host." >&2
        exit 1
    fi
    case "$ARCH" in
        aarch64|arm64) PLATFORM="linux/arm64" ;;
        x86_64|amd64)  PLATFORM="linux/amd64" ;;
        *) echo "Unsupported cross-arch target: $ARCH" >&2; exit 1 ;;
    esac
    IMAGE="ubuntu:22.04"
    echo ">> Cross-arch build: launching $PLATFORM container for $ARCH"
    # Use --network=host to avoid the host needing the docker0 bridge +
    # iptables masquerade rules; some kernels / firewalls refuse the
    # veth-pair setup that the default bridge driver does.
    exec docker run --rm \
        --platform "$PLATFORM" \
        --network=host \
        -v "$PROJECT_DIR:/work" -w /work \
        -e DNSB_IN_CROSS_CONTAINER=1 \
        -e ARCH="$ARCH" \
        "$IMAGE" bash -c '
            set -euo pipefail
            apt-get update -qq
            DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
                ca-certificates wget file cmake gcc g++ make pkg-config gettext \
                libgtk-3-dev libglib2.0-dev libcurl4-openssl-dev libssl-dev \
                fuse libfuse2
            ./scripts/build-appimage.sh
        '
fi

BUILD_DIR="$PROJECT_DIR/build-appimage-$ARCH"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="$PROJECT_DIR/.appimage-tools"

mkdir -p "$TOOLS_DIR"

# --- 1. Fetch linuxdeploy + the GTK plugin if not already cached --------
case "$ARCH" in
    x86_64)  LD_ARCH="x86_64"  ;;
    aarch64|arm64) LD_ARCH="aarch64" ;;
    *) echo "Unsupported ARCH: $ARCH" >&2; exit 1 ;;
esac
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-$LD_ARCH.AppImage"
GTK_PLUGIN="$TOOLS_DIR/linuxdeploy-plugin-gtk.sh"

if [[ ! -x "$LINUXDEPLOY" ]]; then
    echo ">> Downloading linuxdeploy ($LD_ARCH)"
    wget -q --show-progress \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$LD_ARCH.AppImage" \
        -O "$LINUXDEPLOY"
    chmod +x "$LINUXDEPLOY"
fi
if [[ ! -x "$GTK_PLUGIN" ]]; then
    echo ">> Downloading linuxdeploy-plugin-gtk"
    wget -q --show-progress \
        "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
        -O "$GTK_PLUGIN"
    chmod +x "$GTK_PLUGIN"
fi

# --- 2. Configure + build + install into AppDir -------------------------
echo ">> Configuring CMake"
rm -rf "$BUILD_DIR" "$APPDIR"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

echo ">> Building"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ">> Installing into AppDir"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

# --- 3. linuxdeploy needs .desktop + icon discoverable. Our install rule
# already places them in standard freedesktop dirs, but stage paths into
# the args explicitly. ---------------------------------------------------
DESKTOP="$APPDIR/usr/share/applications/dnsbenchmark.desktop"
ICON="$APPDIR/usr/share/icons/hicolor/scalable/apps/dnsbenchmark.svg"

# --- 4. Run linuxdeploy + GTK plugin to bundle deps and emit AppImage ---
export PATH="$TOOLS_DIR:$PATH"
export DEPLOY_GTK_VERSION=3
export OUTPUT="DNS-Benchmark-$ARCH.AppImage"
# linuxdeploy refuses to run as root unless we tell it that's OK; required
# inside the cross-build container which runs as root by default.
export APPIMAGE_EXTRACT_AND_RUN=1
# Bleeding-edge distros (Arch, Manjaro, Fedora rawhide) ship binaries with
# the .relr.dyn relocation section, which linuxdeploy's bundled `strip`
# (older binutils) cannot read. Skipping strip makes the AppImage a few
# MB larger but lets it build on any host.
export NO_STRIP=true

echo ">> Running linuxdeploy (bundles GTK 3 + transitive .so deps)"
"$LINUXDEPLOY" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/dnsbenchmark" \
    --desktop-file "$DESKTOP" \
    --icon-file    "$ICON" \
    --plugin gtk \
    --output appimage

echo
echo ">> Done. Artefact:"
ls -lh "$PROJECT_DIR/$OUTPUT"
echo
echo "Upload this single file to the GitHub Releases page. Users on most"
echo "modern Linux distros (glibc-based, $ARCH) can chmod +x and run it"
echo "directly without installing anything."
