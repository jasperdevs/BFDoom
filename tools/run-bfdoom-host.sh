#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"
BF="$ROOT/programs/bfdoom-linked.bf"
BF_GZ="$ROOT/programs/bfdoom-linked.bf.gz"
BF_CACHE="$BF.bfocache"
BF_SNAPSHOT="$BF.bfosnap"
WAD="$ROOT/data/DOOM1.WAD"
WAD_GZ="$ROOT/data/doom1.wad.gz"
CAPTURE="$ROOT/build/bfdoom-first-frame.ppm"
RESTORED_SNAPSHOT=0

sync_snapshot_mtime() {
  if [ ! -f "$BF" ] || [ ! -f "$BF_SNAPSHOT" ]; then
    return
  fi

  if command -v node >/dev/null 2>&1; then
    node -e 'const fs=require("fs"); const bf=process.argv[1]; const snap=process.argv[2]; const mtime=BigInt(Math.floor(fs.statSync(bf).mtimeMs/1000)); const fd=fs.openSync(snap,"r+"); const b=Buffer.alloc(8); b.writeBigUInt64LE(mtime); fs.writeSync(fd,b,0,8,8); fs.closeSync(fd);' "$BF" "$BF_SNAPSHOT"
  elif command -v node.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
    node.exe -e 'const fs=require("fs"); const bf=process.argv[1]; const snap=process.argv[2]; const mtime=BigInt(Math.floor(fs.statSync(bf).mtimeMs/1000)); const fd=fs.openSync(snap,"r+"); const b=Buffer.alloc(8); b.writeBigUInt64LE(mtime); fs.writeSync(fd,b,0,8,8); fs.closeSync(fd);' "$(wslpath -w "$BF")" "$(wslpath -w "$BF_SNAPSHOT")"
  fi
}

if [ ! -f "$BF" ]; then
  if [ -f "$BF_GZ" ]; then
    mkdir -p "$ROOT/programs"
    gzip -dk "$BF_GZ"
  else
    "$ROOT/tools/build-bfdoom-linked.sh"
  fi
fi

if [ ! -f "$BF_CACHE" ] && [ -f "$BF_CACHE.gz" ]; then
  gzip -dk "$BF_CACHE.gz"
fi

if [ ! -f "$BF_SNAPSHOT" ] && [ -f "$BF_SNAPSHOT.gz" ]; then
  gzip -dk "$BF_SNAPSHOT.gz"
  RESTORED_SNAPSHOT=1
fi

if [ "$RESTORED_SNAPSHOT" = 1 ]; then
  sync_snapshot_mtime
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
  bash "$ROOT/tools/build-bfopt.sh" >/dev/null
fi

if [ -t 2 ]; then
  printf '\033[8;62;180t' >&2
fi
"$ELVM/out/bfopt" -doom-host -wad "$WAD" -capture "$CAPTURE" "$BF"
