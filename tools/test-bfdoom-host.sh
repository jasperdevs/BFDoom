#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"
BF="$ROOT/programs/bfdoom-linked.bf"
WAD="$ROOT/data/DOOM1.WAD"

if [ ! -f "$BF" ]; then
  "$ROOT/tools/build-bfdoom-linked.sh"
fi

if [ ! -f "$WAD" ]; then
  echo "missing WAD: $WAD" >&2
  exit 1
fi

if [ ! -x "$ELVM/out/bfopt" ] || [ "$ELVM/tools/bfopt.cc" -nt "$ELVM/out/bfopt" ]; then
  bash "$ROOT/tools/build-bfopt.sh" >/dev/null
fi

cd "$ROOT"
mkdir -p build

type_slow() {
  local text="$1"
  local delay="${2:-0.03}"
  local hold="${3:-0.25}"
  local i
  for ((i = 0; i < ${#text}; i++)); do
    printf '%s' "${text:i:1}"
    sleep "$delay"
  done
  sleep "$hold"
}

rm -f build/bfdoom-smoke-empty.ppm build/bfdoom-smoke-empty.err
timeout 10 "$ELVM/out/bfopt" -doom-host -wad "$WAD" \
  -capture build/bfdoom-smoke-empty.ppm "$BF" \
  </dev/null >/dev/null 2>build/bfdoom-smoke-empty.err
grep -q "actors spawned=58 skipped_skill=24 skipped_single=14 ambush=18" build/bfdoom-smoke-empty.err
grep -q "frame 1" build/bfdoom-smoke-empty.err
test -s build/bfdoom-smoke-empty.ppm

rm -f build/bfdoom-smoke-scripted.ppm build/bfdoom-smoke-scripted.err
printf "wwwwdd f q" | timeout 10 "$ELVM/out/bfopt" -doom-host -wad "$WAD" \
  -capture build/bfdoom-smoke-scripted.ppm "$BF" \
  >/dev/null 2>build/bfdoom-smoke-scripted.err
grep -q "fire weapon=2 ammo=49" build/bfdoom-smoke-scripted.err
test -s build/bfdoom-smoke-scripted.ppm

rm -f build/bfdoom-smoke-sprite.ppm build/bfdoom-smoke-sprite.err
printf "dddddddddd" | timeout 10 "$ELVM/out/bfopt" -doom-host -wad "$WAD" \
  -capture build/bfdoom-smoke-sprite.ppm "$BF" \
  >/dev/null 2>build/bfdoom-smoke-sprite.err
grep -q "frame 1" build/bfdoom-smoke-sprite.err
test -s build/bfdoom-smoke-sprite.ppm

rm -f build/bfdoom-smoke-rotated-sprite.ppm build/bfdoom-smoke-rotated-sprite.err
type_slow "aaaaaaaaaaaa" | timeout 10 "$ELVM/out/bfopt" -doom-host -wad "$WAD" \
  -capture build/bfdoom-smoke-rotated-sprite.ppm "$BF" \
  >/dev/null 2>build/bfdoom-smoke-rotated-sprite.err
grep -q "frame 1" build/bfdoom-smoke-rotated-sprite.err
test -s build/bfdoom-smoke-rotated-sprite.ppm

rm -f build/bfdoom-smoke-use.ppm build/bfdoom-smoke-use.err
printf "aaaaaaaaaaaaaaaaaaaaaaaae" | timeout 10 "$ELVM/out/bfopt" -doom-host -wad "$WAD" \
  -capture build/bfdoom-smoke-use.ppm "$BF" \
  >/dev/null 2>build/bfdoom-smoke-use.err
grep -q "use opened_line=" build/bfdoom-smoke-use.err
test -s build/bfdoom-smoke-use.ppm

rm -f build/bfdoom-smoke-arrows.ppm build/bfdoom-smoke-arrows.err
printf "\033[C\033[C" | timeout 10 "$ELVM/out/bfopt" -doom-host -wad "$WAD" \
  -capture build/bfdoom-smoke-arrows.ppm "$BF" \
  >/dev/null 2>build/bfdoom-smoke-arrows.err
grep -q "frame 1" build/bfdoom-smoke-arrows.err
test -s build/bfdoom-smoke-arrows.ppm

rm -f build/bfdoom-smoke-pickup.ppm build/bfdoom-smoke-pickup.err
type_slow "aaaaaaaaawwwwwwwwwwwwwwww" 0.02 0.5 | timeout 10 "$ELVM/out/bfopt" -doom-host -wad "$WAD" \
  -capture build/bfdoom-smoke-pickup.ppm "$BF" \
  >/dev/null 2>build/bfdoom-smoke-pickup.err
grep -q "pickup type=2014" build/bfdoom-smoke-pickup.err
test -s build/bfdoom-smoke-pickup.ppm

rm -f build/bfdoom-smoke-weapon.ppm build/bfdoom-smoke-weapon.err
printf "2f" | timeout 10 "$ELVM/out/bfopt" -doom-host -wad "$WAD" \
  -capture build/bfdoom-smoke-weapon.ppm "$BF" \
  >/dev/null 2>build/bfdoom-smoke-weapon.err
grep -q "weapon slot=2" build/bfdoom-smoke-weapon.err
grep -q "fire weapon=2 ammo=49" build/bfdoom-smoke-weapon.err
! grep -q "alert sound=[1-9]" build/bfdoom-smoke-weapon.err
test -s build/bfdoom-smoke-weapon.ppm

echo "bfdoom host smoke passed"
