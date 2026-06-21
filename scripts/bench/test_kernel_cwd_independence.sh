#!/usr/bin/env bash
# Regression test: generic-kernel generation must work from ANY working dir.
#
# crackalack_gen's generic kernel chain (CUDA/crackalack.cu -> rt.cu ->
# #include "shared.h") used to resolve includes only relative to CWD, so it
# silently produced a 0-byte table unless run from the repo root (where
# shared.h lives).  The CUDA include resolver now also searches the directory
# of the running executable.  This test pins that: gen from a bare temp dir
# with NO CUDA/ symlink and NO shared.h must yield a real table.
#
# Requires a CUDA GPU (Linux/CUDA build).  Skips cleanly if not applicable.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$DIR/../.." && pwd)"
GEN="$REPO/crackalack_gen"

[[ -x "$GEN" ]] || { echo "SKIP: $GEN not built"; exit 0; }
[[ -f "$REPO/CUDA/crackalack.cu" ]] || { echo "SKIP: not a CUDA build (no CUDA/ kernels)"; exit 0; }

# shellcheck source=/dev/null
source "$DIR/lib/gpu_lock.sh"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Sanity: the foreign work dir must NOT contain the kernel dir or shared.h,
# otherwise the test would pass for the wrong reason.
[[ ! -e "$work/CUDA" && ! -e "$work/shared.h" ]] || { echo "FAIL: work dir is contaminated"; exit 1; }

# Generic kernel is selected by a non-NTLM8/9 config (here: NetNTLMv1 byte 7-7,
# short chain).  Run from $work, NOT the repo root.
( cd "$work" && with_gpu_lock "$GEN" netntlmv1 byte 7 7 0 1000 1024 0 >/dev/null 2>&1 ) \
  || { echo "FAIL: crackalack_gen exited non-zero from foreign CWD"; exit 1; }

table="$(ls "$work"/*.rt 2>/dev/null | head -n1)"
[[ -n "$table" ]] || { echo "FAIL: no .rt produced"; exit 1; }

size="$(stat -c %s "$table")"
# 1024 chains * 16 bytes/chain (8-byte start + 8-byte end) = 16384.
[[ "$size" -eq 16384 ]] || { echo "FAIL: table is $size bytes (expected 16384 -- empty/short means include resolution failed)"; exit 1; }

echo "PASS"
