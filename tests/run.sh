#!/usr/bin/env bash
# Host tests: pure watch logic (C, built with the host compiler) and phone logic (Node).
# usage: tests/run.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

${CC:-cc} -std=c99 -Wall -Wextra -Werror -o "$OUT/test_logic" \
  "$ROOT/tests/c/test_logic.c" "$ROOT/src/c/td_logic.c"
"$OUT/test_logic"
node "$ROOT/tests/js/test_phone.js"
