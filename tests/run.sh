#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/tests/test_codec.cpp" -o "$TMP/test_codec"
"$TMP/test_codec"

g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/tests/test_ir_decode.cpp" -o "$TMP/test_ir_decode"
"$TMP/test_ir_decode"

g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/tests/test_ir_ring.cpp" -o "$TMP/test_ir_ring"
"$TMP/test_ir_ring"

# Checks the recorded captures of the original remote, if there are any.
g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/tests/test_ir_captures.cpp" -o "$TMP/test_ir_captures"
(cd "$ROOT" && "$TMP/test_ir_captures")

g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/tests/test_ir_audit.cpp" -o "$TMP/test_ir_audit"
"$TMP/test_ir_audit"

if command -v python3 >/dev/null 2>&1; then
    python3 "$ROOT/tests/test_ir_record.py"
    # Needs a pseudo terminal, so it skips itself where there is none.
    python3 "$ROOT/tests/test_ir_record_cancel.py"
fi
