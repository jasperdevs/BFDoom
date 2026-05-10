#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"
DOOM="$ROOT/vendor/doomgeneric/doomgeneric"
PORT_LIBC="$ROOT/ports/elvm-libc"
SRC="$ROOT/examples/doom-random-harness.c"
EIR="$ROOT/build/doom-random-harness.eir"
BF="$ROOT/programs/doom-random-harness.bf"

mkdir -p "$ROOT/build" "$ROOT/programs"

strip_to_brainfuck() {
  local file="$1"
  local tmp="$file.tmp"
  LC_ALL=C tr -cd '><+.,[]-' < "$file" > "$tmp"
  mv "$tmp" "$file"
}

cd "$ELVM"
./out/8cc -S -D__eir__ -DINT_MIN=-16777216 -DSHRT_MAX=32767 -DEISDIR=21 -DSEEK_SET=0 -DSEEK_END=2 -I"$PORT_LIBC" -I. -Ilibc -Iout -I"$DOOM" -o "$EIR" "$SRC"
./out/elc -bf "$EIR" > "$BF"
strip_to_brainfuck "$BF"
./out/bfopt "$BF"
