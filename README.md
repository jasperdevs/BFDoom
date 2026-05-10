<h1 align="center">BFDoom</h1>

<p align="center">Doom compiled to Brainfuck, with a playable WAD-backed host path.</p>

<p align="center">
  <strong>Brainfuck</strong> / <strong>DoomGeneric</strong> / <strong>ELVM</strong> / <strong>WAD-backed</strong>
</p>

<p align="center">
  <img src="docs/bfdoom-preview.png" alt="BFDoom gameplay preview" width="720" />
</p>

<p align="center">
  <img src="docs/e1m1-route-map.png" alt="E1M1 route map" width="720" />
</p>

## Quickstart

```bash
git clone https://github.com/jasperdevs/BFDoom.git
cd BFDoom
npm run play:bfdoom
```

The main Brainfuck artifact is:

```text
programs/bfdoom-linked.bf.gz
```

On first run it restores to:

```text
programs/bfdoom-linked.bf
```

That raw file is not committed because it is about 550 MB.

## Controls

- `W` / `S` or Up / Down: move
- `A` / `D` or Left / Right: turn
- `Space` / `F`: fire
- `1`-`7`: switch owned weapons
- `E`: use
- `Q` / `Esc`: quit

## Setup

Requirements:

- Node.js 20+
- Windows with WSL, or Linux with `bash`
- `make`, `g++`, `gzip`, and standard build tools

On Ubuntu/WSL:

```bash
sudo apt update
sudo apt install -y build-essential make gzip
```

## Verify

```bash
npm run test:bfdoom-host
npm test
```

## What Is Here

- `programs/bfdoom-linked.bf.gz`: generated Brainfuck Doom artifact
- `data/doom1.wad.gz`: Doom shareware IWAD archive used by the host path
- `vendor/DOOM`: original Doom source
- `vendor/doomgeneric`: portable Doom source tree
- `vendor/elvm`: compiler path used to emit Brainfuck
- `vendor/elvm/tools/bfopt.cc`: optimized Brainfuck runner and current playable host bridge

## Status

BFDoom is playable, WAD-backed, and real Brainfuck-port work. It is not a finished 1:1 Doom replacement yet. The largest remaining gaps are full renderer parity, sound, menus, and the complete Doom monster/state runtime.

## License

Doom and DoomGeneric code are GPL-2.0. ELVM is MIT licensed. See the vendor license files for upstream details.
