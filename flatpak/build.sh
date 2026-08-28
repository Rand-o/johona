#!/usr/bin/env bash
# build.sh — build the Johona Wallpaper Flatpak (spec §12).
#
# Produces a local --user installation (parity with the kWallpaper flow):
#   flatpak/build.sh          # build + install to the --user remote
#   flatpak/build.sh bundle   # also produce a .flatpak bundle
#
# Requires: flatpak, flatpak-builder, and the org.kde.Platform//6.9 +
# org.kde.Sdk//6.9 runtimes (flathub remote).

set -euo pipefail

cd "$(dirname "$0")"
REPO_ROOT="$(cd .. && pwd)"
APP_ID="top.spelunk.johona"
MANIFEST="top.spelunk.johona.yaml"
BUILD_DIR="build"
BUNDLE="${APP_ID}.flatpak"

ensure_runtimes() {
    local remote="${1:-flathub}"
    for rt in "org.kde.Platform//6.9" "org.kde.Sdk//6.9"; do
        if ! flatpak list --app --runtime 2>/dev/null | grep -q "^${rt%%//*}"; then
            echo "Installing $rt from $remote…"
            flatpak remote-add-if-not-exists --if-not-exists "$remote" \
                https://dl.flathub.org/repo/flathub.flatpakrepo 2>/dev/null || true
            flatpak install -y --noninteractive --runtime "$remote" "$rt"
        fi
    done
}

main() {
    command -v flatpak-builder >/dev/null || {
        echo "flatpak-builder is required" >&2; exit 1; }
    ensure_runtimes

    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"

    echo "==> flatpak-builder (source: $REPO_ROOT)"
    flatpak-builder --force-clean --keep-build-dirs=0 \
        --repo="$BUILD_DIR/repo" \
        --install-deps-from=flathub \
        --user \
        "$BUILD_DIR/app" "$MANIFEST"

    echo
    echo "Installed. Run with:  flatpak run $APP_ID"

    if [[ "${1:-}" == "bundle" ]]; then
        echo "==> building bundle"
        flatpak build-bundle "$BUILD_DIR/repo" "$BUNDLE" "$APP_ID"
        echo "Bundle: $(pwd)/$BUNDLE"
    fi
}

main "$@"
