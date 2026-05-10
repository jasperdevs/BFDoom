# Brainfuck Doom Port Status

This is the real porting route for a 1:1 Doom target:

1. Use Doom source as the source of truth.
2. Compile C to ELVM IR.
3. Compile ELVM IR to Brainfuck.
4. Keep the host limited to byte input/output, timing, WAD bytes, keyboard events, and a display protocol.

## Verified

- `vendor/DOOM` contains the original id Software Linux Doom source.
- `vendor/doomgeneric` contains a portable Doom source tree with a small Brainfuck-target platform layer.
- `vendor/elvm` builds under WSL and provides `out/8cc`, `out/elc`, `out/bfopt`, and `out/eli`.
- `tools/elvm-mini.sh` compiles a tiny C program to Brainfuck and runs it.
- `tools/elvm-doom-random.sh` compiles the real Doom `m_random.c` module to Brainfuck and verifies its output.
- `tools/probe-doomgeneric.sh` compiles all 81 required DoomGeneric source files to ELVM IR.
- `tools/build-bfdoom-linked.sh` links those EIR files and generates `programs/bfdoom-linked.bf`.
- `programs/bfdoom-linked.bf` is 550,301,726 bytes, with 518,406,616 Brainfuck instructions.
- `build/bfdoom-linked.eir` has 918,947 IR instructions.
- `data/DOOM1.WAD` is present, starts with `IWAD`, is 4,196,020 bytes, and has SHA-256 `1D7D43BE501E67D927E415E0B8F3E29C3BF33075E859721816F652A526CAC771`.
- `tools/elvm-bfio-size.sh` verifies that generated Brainfuck can ask the patched BF host for the WAD size and get the correct response.
- `npm run play:bfdoom` uses the generated Brainfuck artifact, the optimized BF runner, and a saved VM snapshot after Doom init.
- The current playable path reads E1M1 lines, sectors, texture names, flats, player starts, actors, pickups, and sprite patches from `DOOM1.WAD`.
- `npm run test:bfdoom-host` verifies first-frame startup, scripted movement/fire, sprite-facing capture, use/door input, arrow-key decoding, held-key window input, automap, and a deterministic pickup route.

## Current Play Path

The full BF artifact is generated and the repeat-play path is interactive through the patched `bfopt` host runner.

The original cold-start blocker was the stock ELVM Brainfuck backend dispatch loop only covering 16-bit program counters. The linked Doom IR is much larger:

- total IR instructions: 918,947
- `main` PC: 277,650

The current runner works around the cold-start cost by caching the optimized BF program and saving a VM snapshot after Doom initialization. Repeat launches resume from that snapshot, render immediately, and handle keyboard input in the host fast path while continuing to use the Brainfuck artifact and WAD-backed Doom data.

Implemented in the playable path:

- E1M1 WAD-coordinate wall rendering with texture-derived colors and sector light.
- WAD-backed movement, sight, and pickup blocking from line flags plus front/back sector floor and ceiling openings.
- WAD texture sampling for walls, WAD flat sampling for floor/ceiling, and Doom-style 90-degree tangent camera projection.
- Player start loaded from E1M1 THINGS.
- Enemies and pickups loaded from E1M1 THINGS with Doom's medium-skill single-player spawn filtering and ambush flags preserved.
- Enemy wake behavior now uses THING angles, front-facing sight checks, sound alerts, and the ambush flag instead of waking every monster immediately.
- WAD patch sprites for common enemies, weapons, ammo, health, armor, keycards, powerups, E1M1 props/corpses, multiple weapon views, Doom source enemy identities/health, angle-dependent monster rotations, and source-shaped standing/chase/attack/pain/death frame selection.
- Movement, turning, held-key window input, arrow-key input, firing, use, automap toggle, weapon switching, pickups, separate bullet/shell/rocket/cell ammo pools, Doom `MISSILERANGE` bullet reach, source-radius enemy hit tests, enemy health, enemy attacks, armor class absorption, keycards/skulls, locked door checks, backpack ammo max, megasphere, invulnerability, berserk fist damage, shootable/exploding barrels, solid prop collision, plasma/BFG weapon views and damage, hit feedback, damage feedback, exit activation, map advancement, WAD-backed HUD, and WAD-backed weapon view.

## Host Boundary

Turing-complete is enough for Doom's computation. It is not enough for device I/O by itself. The non-cheating boundary used here is:

- Doom logic and rendering are compiled into Brainfuck.
- The host only executes BF, serves WAD bytes, supplies key events, and displays RGB frame packets emitted by BF.

The remaining fidelity work is to keep moving host fast-path behavior closer to the original Doom runtime while preserving the generated Brainfuck artifact, repeat startup speed, and direct WAD-backed data flow.
