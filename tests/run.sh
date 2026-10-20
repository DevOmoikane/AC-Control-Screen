#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/include" "$ROOT/tests/test_codec.cpp" -o "$TMP/test_codec"
"$TMP/test_codec"
