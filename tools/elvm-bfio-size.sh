#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"
OUT_EIR="$ROOT/build/bfio-size.eir"
OUT_BF="$ROOT/programs/bfio-size.bf"
WAD="$ROOT/data/DOOM1.WAD"

mkdir -p "$ROOT/build" "$ROOT/programs"

strip_to_brainfuck() {
  local file="$1"
  local tmp="$file.tmp"
  LC_ALL=C tr -cd '><+.,[]-' < "$file" > "$tmp"
  mv "$tmp" "$file"
}

cd "$ELVM"
./out/8cc -S -D__eir__ -I"$ROOT/ports/elvm-libc" -I. -Ilibc -Iout -o "$OUT_EIR" "$ROOT/examples/bfio-size.c"
./out/elc -bf "$OUT_EIR" > "$OUT_BF"
strip_to_brainfuck "$OUT_BF"
./out/bfopt -doom-host -wad "$WAD" "$OUT_BF" > "$ROOT/build/bfio-size.out" 2> "$ROOT/build/bfio-size.err"

if ! grep -q "Y" "$ROOT/build/bfio-size.err"; then
  echo "BFIO size harness failed" >&2
  cat "$ROOT/build/bfio-size.err" >&2
  exit 1
fi

echo "BFIO WAD size harness passed"
