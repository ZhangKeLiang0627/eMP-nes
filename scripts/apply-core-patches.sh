#!/usr/bin/env bash
# Apply eMP-nes compatibility patches to the pinned SimpleNES submodule
# worktree. The submodule itself stays pristine upstream; the patches are
# re-applied idempotently at cmake configure time so every build (CI or
# local) sees the same patched core.
#
# Current patch set:
#   simplenes-gcc64.patch  - APU::setup_frame_counter: std::ref(derived) ->
#     vector<reference_wrapper<Base>> needs libstdc++ P0357 (C++20); GCC 6.4
#     (T113 musl) lacks it. Cast each slot to FrameClockable& first.
set -eu

SN_DIR="${1:?usage: apply-core-patches.sh <simplenes-dir>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MARKER="${SN_DIR}/.empnes-patched"

[ -f "${MARKER}" ] && exit 0          # already applied
[ -f "${SN_DIR}/src/APU/APU.cpp" ] || { echo "[patch] simplenes not found: ${SN_DIR}"; exit 1; }

for p in cmake/simplenes-gcc64.patch; do
    echo "[patch] applying ${p} to ${SN_DIR}"
    patch -d "${SN_DIR}" -p1 < "${ROOT}/${p}"
done

touch "${MARKER}"
echo "[patch] done"
