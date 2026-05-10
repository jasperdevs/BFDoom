import fs from "node:fs";
import process from "node:process";

const args = process.argv.slice(2);
const file = args[0];

if (!file) {
  console.error("Usage: node src/bf-runner.mjs <program.bf> [--input text] [--max-ops n]");
  process.exit(1);
}

let scriptedInput = "";
let maxOps = 100_000_000;

for (let i = 1; i < args.length; i += 1) {
  if (args[i] === "--input") {
    scriptedInput = args[i + 1] ?? "";
    i += 1;
  } else if (args[i] === "--max-ops") {
    maxOps = Number(args[i + 1] ?? maxOps);
    i += 1;
  }
}

const source = fs.readFileSync(file, "utf8").replace(/[^\[\]\<\>\+\-\.\,]/g, "");
const jumps = new Map();
const stack = [];

for (let i = 0; i < source.length; i += 1) {
  if (source[i] === "[") stack.push(i);
  if (source[i] === "]") {
    const open = stack.pop();
    if (open === undefined) throw new Error(`Unmatched ] at ${i}`);
    jumps.set(open, i);
    jumps.set(i, open);
  }
}

if (stack.length > 0) throw new Error(`Unmatched [ at ${stack.pop()}`);

const tape = new Uint8Array(1 << 20);
let ptr = 0;
let ip = 0;
let ops = 0;
let inputOffset = 0;
const interactive = scriptedInput.length === 0 && process.stdin.isTTY;

if (interactive) {
  process.stdin.setRawMode(true);
  process.stdin.resume();
  process.stdin.setEncoding("utf8");
}

function readByte() {
  if (scriptedInput.length > 0) {
    if (inputOffset >= scriptedInput.length) return "q".charCodeAt(0);
    const code = scriptedInput.charCodeAt(inputOffset);
    inputOffset += 1;
    return code & 255;
  }

  if (!interactive) {
    const buffer = Buffer.alloc(1);
    const read = fs.readSync(0, buffer, 0, 1, null);
    return read === 0 ? "q".charCodeAt(0) : buffer[0];
  }

  const buffer = Buffer.alloc(1);
  const read = fs.readSync(0, buffer, 0, 1, null);
  return read === 0 ? 0 : buffer[0];
}

try {
  while (ip < source.length) {
    ops += 1;
    if (ops > maxOps) throw new Error(`Exceeded max ops (${maxOps})`);

    switch (source[ip]) {
      case ">":
        ptr += 1;
        if (ptr >= tape.length) throw new Error("Tape pointer overflow");
        break;
      case "<":
        if (ptr === 0) throw new Error("Tape pointer underflow");
        ptr -= 1;
        break;
      case "+":
        tape[ptr] = (tape[ptr] + 1) & 255;
        break;
      case "-":
        tape[ptr] = (tape[ptr] - 1) & 255;
        break;
      case ".":
        process.stdout.write(String.fromCharCode(tape[ptr]));
        break;
      case ",":
        tape[ptr] = readByte();
        break;
      case "[":
        if (tape[ptr] === 0) ip = jumps.get(ip);
        break;
      case "]":
        if (tape[ptr] !== 0) ip = jumps.get(ip);
        break;
    }

    ip += 1;
  }
} finally {
  if (interactive) {
    process.stdin.setRawMode(false);
    process.stdin.pause();
  }
}
