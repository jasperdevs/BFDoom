import fs from "node:fs";
import path from "node:path";

const C = {
  running: 0,
  state: 1,
  dispatch: 2,
  tmp: 3,
  backup: 4,
  flag: 5,
  input: 6,
  out: 7
};

let p = 0;
let code = "";

function move(to) {
  while (p < to) {
    code += ">";
    p += 1;
  }
  while (p > to) {
    code += "<";
    p -= 1;
  }
}

function clear(cell) {
  move(cell);
  code += "[-]";
}

function add(cell, n) {
  move(cell);
  code += n >= 0 ? "+".repeat(n) : "-".repeat(-n);
}

function set(cell, n) {
  clear(cell);
  add(cell, n);
}

function copyPreserve(from, to, backup) {
  clear(to);
  clear(backup);
  move(from);
  code += "[";
  code += "-";
  move(to);
  code += "+";
  move(backup);
  code += "+";
  move(from);
  code += "]";
  move(backup);
  code += "[";
  code += "-";
  move(from);
  code += "+";
  move(backup);
  code += "]";
}

function equalFlag(source, value) {
  copyPreserve(source, C.tmp, C.backup);
  add(C.tmp, -value);
  set(C.flag, 1);
  move(C.tmp);
  code += "[";
  clear(C.flag);
  clear(C.tmp);
  move(C.tmp);
  code += "]";
}

function ifFlag(body) {
  move(C.flag);
  code += "[";
  body();
  clear(C.flag);
  move(C.flag);
  code += "]";
}

function emit(text) {
  clear(C.out);
  let outValue = 0;
  move(C.out);
  for (const ch of text) {
    const target = ch.charCodeAt(0) & 255;
    const up = (target - outValue + 256) & 255;
    const down = (outValue - target + 256) & 255;
    code += up <= down ? "+".repeat(up) : "-".repeat(down);
    code += ".";
    outValue = target;
  }
}

const map = [
  "########",
  "#......#",
  "#.##...#",
  "#..D#..#",
  "#..##..#",
  "#......#",
  "########"
];

const dirs = [
  { name: "N", dx: 0, dy: -1, icon: "^" },
  { name: "E", dx: 1, dy: 0, icon: ">" },
  { name: "S", dx: 0, dy: 1, icon: "v" },
  { name: "W", dx: -1, dy: 0, icon: "<" }
];

const positions = [
  { x: 2, y: 5 },
  { x: 3, y: 5 },
  { x: 4, y: 5 },
  { x: 5, y: 5 },
  { x: 5, y: 4 },
  { x: 5, y: 3 },
  { x: 5, y: 2 },
  { x: 4, y: 2 },
  { x: 3, y: 3 },
  { x: 2, y: 3 },
  { x: 2, y: 4 }
];

const positionIndex = new Map(positions.map((pos, idx) => [`${pos.x},${pos.y}`, idx]));
const stateId = (pos, dir) => pos * 4 + dir;
const states = [];

for (let pos = 0; pos < positions.length; pos += 1) {
  for (let dir = 0; dir < dirs.length; dir += 1) {
    states.push({ id: stateId(pos, dir), pos, dir });
  }
}

function tile(x, y) {
  return map[y]?.[x] ?? "#";
}

function frameFor(state) {
  const pos = positions[state.pos];
  const dir = dirs[state.dir];
  const left = dirs[(state.dir + 3) % 4];
  const right = dirs[(state.dir + 1) % 4];
  const front1 = tile(pos.x + dir.dx, pos.y + dir.dy);
  const front2 = tile(pos.x + dir.dx * 2, pos.y + dir.dy * 2);
  const left1 = tile(pos.x + left.dx, pos.y + left.dy);
  const right1 = tile(pos.x + right.dx, pos.y + right.dy);
  const wall = front1 === "#";
  const door = front1 === "D" || front2 === "D";

  const view = wall
    ? [
        "+==============================+",
        "|\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\|",
        "|\\\\\\\\\\\\\\\\ STONE WALL \\\\\\\\\\\\\\\\\\\\|",
        "|\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\|",
        "+==============================+"
      ]
    : door
      ? [
          "+==============================+",
          "|            ____              |",
          "|           |EXIT|             |",
          "|           |____|             |",
          "+==============================+"
        ]
      : [
          "+==============================+",
          "|      \\                /      |",
          "|       \\              /       |",
          "|        \\____________/        |",
          "+==============================+"
        ];

  const radar = map.map((row, y) =>
    row.split("").map((ch, x) => (x === pos.x && y === pos.y ? dir.icon : ch)).join("")
  );

  return [
    "\x1b[2J\x1b[H",
    "BRAINDOOM.BF  pure Brainfuck game logic\n",
    "W move  A/D turn  Q quit\n\n",
    ...view.map((line) => `${line}\n`),
    `\nFacing ${dir.name}  front:${front1}  left:${left1}  right:${right1}\n\n`,
    ...radar.map((line) => `${line}\n`),
    "\n> "
  ].join("");
}

function nextFor(state, key) {
  if (key === "q") return 255;
  if (key === "a") return stateId(state.pos, (state.dir + 3) % 4);
  if (key === "d") return stateId(state.pos, (state.dir + 1) % 4);
  if (key === "w") {
    const pos = positions[state.pos];
    const dir = dirs[state.dir];
    const nx = pos.x + dir.dx;
    const ny = pos.y + dir.dy;
    if (tile(nx, ny) === "#") return state.id;
    if (tile(nx, ny) === "D") return 254;
    const nextPos = positionIndex.get(`${nx},${ny}`);
    return nextPos === undefined ? state.id : stateId(nextPos, state.dir);
  }
  return state.id;
}

function handleInputFor(state) {
  move(C.input);
  code += ",";
  for (const key of ["q", "a", "d", "w"]) {
    equalFlag(C.input, key.charCodeAt(0));
    ifFlag(() => set(C.state, nextFor(state, key)));
  }
}

set(C.running, 1);
set(C.state, 0);

move(C.running);
code += "[";
copyPreserve(C.state, C.dispatch, C.backup);

for (const state of states) {
  equalFlag(C.dispatch, state.id);
  ifFlag(() => {
    emit(frameFor(state));
    handleInputFor(state);
  });
}

equalFlag(C.dispatch, 254);
ifFlag(() => {
  emit("\x1b[2J\x1b[HBRAINDOOM.BF\n\nYou found the exit switch.\nThe run ends here.\n");
  set(C.running, 0);
});

equalFlag(C.dispatch, 255);
ifFlag(() => {
  emit("\x1b[2J\x1b[HBRAINDOOM.BF closed.\n");
  set(C.running, 0);
});

move(C.running);
code += "]";

fs.mkdirSync("programs", { recursive: true });
fs.writeFileSync(path.join("programs", "braindoom.bf"), code);
console.log(`Generated programs/braindoom.bf (${code.length} Brainfuck instructions)`);
