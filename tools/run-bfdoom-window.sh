#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"
BF="$ROOT/programs/bfdoom-linked.bf"
BF_GZ="$ROOT/programs/bfdoom-linked.bf.gz"
WAD="$ROOT/data/DOOM1.WAD"
WAD_GZ="$ROOT/data/doom1.wad.gz"
CAPTURE="$ROOT/build/bfdoom-window-first-frame.ppm"

if [ ! -f "$BF" ]; then
  if [ -f "$BF_GZ" ]; then
    mkdir -p "$ROOT/programs"
    gzip -dc "$BF_GZ" > "$BF"
  else
    "$ROOT/tools/build-bfdoom-linked.sh"
  fi
fi

if [ ! -f "$WAD" ]; then
  if [ -f "$WAD_GZ" ]; then
    mkdir -p "$ROOT/data"
    gzip -dc "$WAD_GZ" > "$WAD"
  else
    echo "missing WAD: $WAD" >&2
    exit 1
  fi
fi

if [ ! -x "$ELVM/out/bfopt" ] || [ "$ELVM/tools/bfopt.cc" -nt "$ELVM/out/bfopt" ]; then
  cd "$ELVM"
  make out/bfopt >/dev/null
fi

"$ELVM/out/bfopt" -doom-host -window-stream -wad "$WAD" -capture "$CAPTURE" "$BF"
