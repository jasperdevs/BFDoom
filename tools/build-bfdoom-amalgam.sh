#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELVM="$ROOT/vendor/elvm"
DOOM="$ROOT/vendor/doomgeneric/doomgeneric"
PORT_LIBC="$ROOT/ports/elvm-libc"
AMALGAM="$ROOT/build/bfdoom-amalgam.c"
EIR="$ROOT/build/bfdoom-amalgam.eir"
BF="$ROOT/programs/bfdoom-amalgam.bf"

SOURCES=(
  dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main
  d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom
  i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox m_cheat
  m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy
  p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg
  p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw
  r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff
  s_sound tables v_video wi_stuff w_checksum w_file w_main w_wad z_zone
  w_file_stdc i_input i_video doomgeneric
)

mkdir -p "$ROOT/build" "$ROOT/programs"

{
  printf '#include "../vendor/elvm/libc/_builtin.h"\n'
  printf '#include "../ports/elvm-libc/runtime.c"\n'
  for base in "${SOURCES[@]}"; do
    printf '#include "../vendor/doomgeneric/doomgeneric/%s.c"\n' "$base"
  done
  printf '#include "../ports/bfdoom/doomgeneric_bf.c"\n'
} > "$AMALGAM"

cd "$ELVM"
./out/8cc -S -D__eir__ -DINT_MIN=-16777216 -DSHRT_MAX=32767 -DEISDIR=21 -DSEEK_SET=0 -DSEEK_END=2 -I"$PORT_LIBC" -I. -Ilibc -Iout -I"$DOOM" -o "$EIR" "$AMALGAM"
./out/elc -bf "$EIR" > "$BF"

printf 'Generated %s\n' "$BF"
