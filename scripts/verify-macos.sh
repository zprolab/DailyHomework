#!/usr/bin/env bash
# Local verification: clean rebuild, all unit tests, no-Chinese-file-name guard,
# and a byte-for-byte comparison of the CLI export against the shell export.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
detect_qt_prefix() {
    if [ -n "${QT_PREFIX:-}" ]; then
        printf '%s' "${QT_PREFIX}"
        return
    fi
    # Official Qt installer kits, newest first.
    local kit
    for kit in $(ls -d "${HOME}"/Qt/*/macos 2>/dev/null | sort -Vr); do
        if [ -x "${kit}/bin/qmake" ]; then
            printf '%s' "${kit}"
            return
        fi
    done
    # Homebrew (Apple silicon / Intel).
    for kit in /opt/homebrew/opt/qt6 /usr/local/opt/qt6; do
        if [ -f "${kit}/lib/cmake/Qt6/Qt6Config.cmake" ]; then
            printf '%s' "${kit}"
            return
        fi
    done
}
QT_PREFIX="$(detect_qt_prefix)"
if [ -z "${QT_PREFIX}" ]; then
    echo "FAIL: Qt 6 not found. Set QT_PREFIX or install Qt 6 (Homebrew qt6 or the official installer)." >&2
    exit 1
fi
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
APP="${BUILD_DIR}/apps/qt/DaHoWork"

echo "== file names must be ASCII =="
NON_ASCII="$(cd "${ROOT}" && find . -path ./.git -prune -o \
    -name 'build*' -prune -o -print 2>/dev/null | LC_ALL=C grep '[^ -~]' || true)"
if [ -n "${NON_ASCII}" ]; then
    echo "FAIL: these file or directory names contain non-ASCII characters:" >&2
    echo "${NON_ASCII}" >&2
    exit 1
fi
echo "ok"

echo "== Qt =="
echo "${QT_PREFIX} ($("${QT_PREFIX}/bin/qmake" -query QT_VERSION 2>/dev/null || echo unknown))"

echo "== configure =="
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX}"

echo "== build =="
cmake --build "${BUILD_DIR}" --parallel "${JOBS}"

echo "== unit tests =="
ctest --test-dir "${BUILD_DIR}" --output-on-failure

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${WORK}/data"
cp "${ROOT}/fixtures/demo/data.json" "${WORK}/data/data.json"

echo "== headless export: CLI =="
# Both exports must run on the same QPA platform: the layout commands are identical
# either way, but the rasterizer (cocoa vs offscreen) is not, so hashes would differ.
QT_QPA_PLATFORM=offscreen "${BUILD_DIR}/tools/dhw_render_cli" --data "${WORK}/data/data.json" \
    --date 2026-10-12 --out "${WORK}/cli.png"

echo "== headless export: shell (same layout engine) =="
QT_QPA_PLATFORM=offscreen "${APP}" --data-dir "${WORK}/data" --date 2026-10-12 \
    --render "${WORK}/app.png"

CLI_HASH="$(shasum -a 256 "${WORK}/cli.png" | cut -d' ' -f1)"
APP_HASH="$(shasum -a 256 "${WORK}/app.png" | cut -d' ' -f1)"
if [ "${CLI_HASH}" = "${APP_HASH}" ]; then
    echo "ok: CLI and shell export are byte identical (${CLI_HASH:0:16}...)"
else
    echo "FAIL: CLI and shell export differ" >&2
    echo "  cli: ${CLI_HASH}" >&2
    echo "  app: ${APP_HASH}" >&2
    exit 1
fi

echo
echo "All checks passed. Artifacts:"
echo "  ${APP}"
echo "  ${BUILD_DIR}/tools/dhw_render_cli"
