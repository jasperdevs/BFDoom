import { execFileSync } from "node:child_process";

const baseArgs = ["src/bf-runner.mjs", "programs/braindoom.bf", "--max-ops", "20000000"];

const quit = execFileSync(process.execPath, [...baseArgs, "--input", "q"], {
  encoding: "utf8"
});

if (!quit.includes("BRAINDOOM.BF closed.")) {
  throw new Error("Quit path did not close cleanly");
}

const exit = execFileSync(process.execPath, [...baseArgs, "--input", "wwdw"], {
  encoding: "utf8"
});

if (!exit.includes("You found the exit switch.")) {
  throw new Error("Exit path did not reach the win state");
}

console.log("Brainfuck smoke tests passed");
