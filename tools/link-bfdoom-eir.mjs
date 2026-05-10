import fs from "node:fs";
import path from "node:path";

const root = path.resolve(import.meta.dirname, "..");
const build = path.join(root, "build");
const probe = path.join(build, "probe");
const out = path.join(build, "bfdoom-linked.eir");

const sources = [
  "runtime",
  "dummy", "am_map", "doomdef", "doomstat", "dstrings", "d_event", "d_items", "d_iwad", "d_loop", "d_main",
  "d_mode", "d_net", "f_finale", "f_wipe", "g_game", "hu_lib", "hu_stuff", "info", "i_cdmus", "i_endoom",
  "i_joystick", "i_scale", "i_sound", "i_system", "i_timer", "memio", "m_argv", "m_bbox", "m_cheat",
  "m_config", "m_controls", "m_fixed", "m_menu", "m_misc", "m_random", "p_ceilng", "p_doors", "p_enemy",
  "p_floor", "p_inter", "p_lights", "p_map", "p_maputl", "p_mobj", "p_plats", "p_pspr", "p_saveg",
  "p_setup", "p_sight", "p_spec", "p_switch", "p_telept", "p_tick", "p_user", "r_bsp", "r_data", "r_draw",
  "r_main", "r_plane", "r_segs", "r_sky", "r_things", "sha1", "sounds", "statdump", "st_lib", "st_stuff",
  "s_sound", "tables", "v_video", "wi_stuff", "w_checksum", "w_file", "w_main", "w_wad", "z_zone",
  "w_file_stdc", "i_input", "i_video", "doomgeneric", "doomgeneric_bf"
];

function eirPath(name) {
  return name === "runtime" ? path.join(build, "runtime.eir") : path.join(probe, `${name}.eir`);
}

function mangleLocalLabels(text, name) {
  const prefix = name.replace(/[^A-Za-z0-9_]/g, "_");
  return text.replace(/\.(L|S)([A-Za-z0-9_.]*)/g, `.${prefix}_$1$2`);
}

let linked = "";
for (const name of sources) {
  const file = eirPath(name);
  if (!fs.existsSync(file)) {
    throw new Error(`Missing EIR file: ${file}`);
  }
  linked += `\n# linked ${name}\n`;
  linked += mangleLocalLabels(fs.readFileSync(file, "utf8"), name);
  linked += "\n";
}

fs.writeFileSync(out, linked);
console.log(`Linked ${sources.length} EIR files into ${out}`);
