#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"
DOOM="$ROOT/vendor/doomgeneric/doomgeneric"
PORT_LIBC="$ROOT/ports/elvm-libc"
RUNTIME_EIR="$ROOT/build/runtime.eir"
LINKED_EIR="$ROOT/build/bfdoom-linked.eir"
BF="$ROOT/programs/bfdoom-linked.bf"

mkdir -p "$ROOT/build" "$ROOT/programs"

strip_to_brainfuck() {
  local file="$1"
  local tmp="$file.tmp"
  LC_ALL=C tr -cd '><+.,[]-' < "$file" > "$tmp"
  mv "$tmp" "$file"
}

cd "$ELVM"
if [ ! -x ./out/8cc ] || [ ! -x ./out/elc ] || [ ! -x ./out/bfopt ]; then
  make out/8cc out/elc out/bfopt >/dev/null
fi

"$ROOT/tools/probe-doomgeneric.sh" > "$ROOT/build/probe-link.log"

cd "$ELVM"
./out/8cc -S -D__eir__ -DINT_MIN=-16777216 -DSHRT_MAX=32767 -DEISDIR=21 -DSEEK_SET=0 -DSEEK_END=2 -I"$PORT_LIBC" -I. -Ilibc -Iout -I"$DOOM" -o "$RUNTIME_EIR" "$ROOT/ports/elvm-libc/runtime.c"

cd "$ROOT"
if command -v node >/dev/null 2>&1; then
  node tools/link-bfdoom-eir.mjs
else
  node.exe tools/link-bfdoom-eir.mjs
fi

cd "$ELVM"
./out/elc -bf "$LINKED_EIR" > "$BF"
strip_to_brainfuck "$BF"

printf 'Generated %s\n' "$BF"
