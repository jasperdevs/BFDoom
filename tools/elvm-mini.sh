#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"
SRC="$ROOT/examples/elvm-mini.c"
EIR="$ROOT/build/elvm-mini.eir"
BF="$ROOT/programs/elvm-mini.bf"

mkdir -p "$ROOT/build" "$ROOT/programs"

strip_to_brainfuck() {
  local file="$1"
  local tmp="$file.tmp"
  LC_ALL=C tr -cd '><+.,[]-' < "$file" > "$tmp"
  mv "$tmp" "$file"
}

cd "$ELVM"
./out/8cc -S -I. -Ilibc -Iout -o "$EIR" "$SRC"
./out/elc -bf "$EIR" > "$BF"
strip_to_brainfuck "$BF"
./out/bfopt "$BF"
