#!/usr/bin/env bash
# Package the generic Linux runtime into a drop-in PortMaster tarball.

set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)"
VERSION="${1:-}"
OUTPUT_DIR="${2:-$ROOT_DIR/dist}"
BINARY="${GTAVC_BINARY:-$ROOT_DIR/build-aarch64/gtavc_linux}"
SDL3_RUNTIME="${GTAVC_SDL3_RUNTIME:-$ROOT_DIR/libSDL3.so.0}"
CONSOLE_UI="${GTAVC_CONSOLE_UI:-1}"

usage() {
    printf 'usage: %s VERSION [OUTPUT_DIR]\n' "$(basename "$0")" >&2
    printf '       GTAVC_BINARY=/path/to/gtavc_linux GTAVC_SDL3_RUNTIME=/path/to/libSDL3.so.0 %s VERSION\n' "$(basename "$0")" >&2
    printf '       GTAVC_CONSOLE_UI=0 %s VERSION  # omit Adjustable.cfg\n' "$(basename "$0")" >&2
}

if [[ -z "$VERSION" || ! "$VERSION" =~ ^[A-Za-z0-9._-]+$ ]]; then
    usage
    exit 2
fi

if [[ "$CONSOLE_UI" != 0 && "$CONSOLE_UI" != 1 ]]; then
    printf 'package: GTAVC_CONSOLE_UI must be 0 or 1\n' >&2
    exit 2
fi

required_files=(
    "$BINARY"
    "$ROOT_DIR/Grand Theft Auto Vice City.sh"
    "$SDL3_RUNTIME"
    "$ROOT_DIR/libc++_shared.so"
    "$ROOT_DIR/assetfile.txt"
)
if [[ "$CONSOLE_UI" == 1 ]]; then
    required_files+=("$ROOT_DIR/Adjustable.cfg")
fi

for required in "${required_files[@]}"; do
    if [[ ! -f "$required" ]]; then
        printf 'package: missing required file: %s\n' "$required" >&2
        exit 1
    fi
done

if [[ ! -x "$BINARY" ]]; then
    printf 'package: binary is not executable: %s\n' "$BINARY" >&2
    exit 1
fi

if ! file -b "$BINARY" | grep -qi 'aarch64'; then
    printf 'package: binary is not AArch64: %s\n' "$BINARY" >&2
    file -b "$BINARY" >&2
    exit 1
fi

read -r manifest_count < "$ROOT_DIR/assetfile.txt"
manifest_lines="$(wc -l < "$ROOT_DIR/assetfile.txt")"
if [[ "$manifest_count" != 120 || "$manifest_lines" != 121 ]]; then
    printf 'package: assetfile.txt must contain 120 entries (header plus 120 lines)\n' >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/gtavc-package.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

PACKAGE_NAME="gtavc-linux-$VERSION"
PACKAGE_DIR="$STAGE/$PACKAGE_NAME"
GAME_DIR="$PACKAGE_DIR/gtavc"
mkdir -p "$GAME_DIR/libs.aarch64"

install -m 0755 "$ROOT_DIR/Grand Theft Auto Vice City.sh" \
    "$PACKAGE_DIR/Grand Theft Auto Vice City.sh"
install -m 0755 "$BINARY" "$GAME_DIR/gtavc_linux"
install -m 0644 "$SDL3_RUNTIME" "$GAME_DIR/libs.aarch64/libSDL3.so.0"
install -m 0644 "$ROOT_DIR/libc++_shared.so" "$GAME_DIR/libc++_shared.so"
install -m 0644 "$ROOT_DIR/assetfile.txt" "$GAME_DIR/assetfile.txt"
if [[ "$CONSOLE_UI" == 1 ]]; then
    install -m 0644 "$ROOT_DIR/Adjustable.cfg" "$GAME_DIR/Adjustable.cfg"
fi

ARCHIVE="$OUTPUT_DIR/$PACKAGE_NAME.tar.gz"
tar --sort=name --mtime='UTC 1970-01-01' \
    --owner=0 --group=0 --numeric-owner \
    -C "$STAGE" -czf "$ARCHIVE" "$PACKAGE_NAME"

# Verify the archive contains exactly the runtime files this script promises.
expected=(
    "$PACKAGE_NAME/"
    "$PACKAGE_NAME/gtavc/"
    "$PACKAGE_NAME/Grand Theft Auto Vice City.sh"
    "$PACKAGE_NAME/gtavc/assetfile.txt"
    "$PACKAGE_NAME/gtavc/gtavc_linux"
    "$PACKAGE_NAME/gtavc/libs.aarch64/"
    "$PACKAGE_NAME/gtavc/libs.aarch64/libSDL3.so.0"
    "$PACKAGE_NAME/gtavc/libc++_shared.so"
)
if [[ "$CONSOLE_UI" == 1 ]]; then
    expected+=("$PACKAGE_NAME/gtavc/Adjustable.cfg")
fi
mapfile -t actual < <(tar -tzf "$ARCHIVE" | LC_ALL=C sort)
mapfile -t expected_sorted < <(printf '%s\n' "${expected[@]}" | LC_ALL=C sort)
if [[ "${actual[*]}" != "${expected_sorted[*]}" ]]; then
    printf 'package: archive contents failed verification\n' >&2
    printf '%s\n' "${actual[@]}" >&2
    exit 1
fi

sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"
printf 'created: %s\n' "$ARCHIVE"
printf 'sha256: %s\n' "$(cut -d ' ' -f 1 "$ARCHIVE.sha256")"
