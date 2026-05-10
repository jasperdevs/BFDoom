#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"

mkdir -p "$ELVM/out"

if command -v g++ >/dev/null 2>&1; then
  g++ -std=c++11 -W -Wall -W -Werror -O3 -DNDEBUG \
    -Wno-missing-field-initializers \
    "$ELVM/tools/bfopt.cc" -o "$ELVM/out/bfopt"
else
  cd "$ELVM"
  make out/bfopt >/dev/null
fi
