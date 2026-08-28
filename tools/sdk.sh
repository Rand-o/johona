#!/usr/bin/env bash
# sdk.sh — run a tool inside the KDE Flatpak SDK (no root, no host installs).
#
# The host has no sudo and the Flatpak runtime is the only complete C++/Qt
# toolchain, so development builds run through:
#
#   tools/sdk.sh ninja -C build
#   tools/sdk.sh ctest --test-dir build
#   tools/sdk.sh cmake -S . -B build -G Ninja
#   tools/sdk.sh g++ --version
#
# The project directory is shared with the sandbox via --filesystem, and
# --share=ipc lets Qt's D-Bus/IPC code paths behave normally.

set -euo pipefail

SDK_RUNTIME="org.kde.Sdk//6.9"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if [[ $# -eq 0 ]]; then
    echo "usage: tools/sdk.sh <tool> [args…]" >&2
    exit 1
fi

exec flatpak run \
    --filesystem="$PROJECT_DIR" \
    --share=ipc \
    --command="$1" \
    "$SDK_RUNTIME" \
    "${@:2}"
