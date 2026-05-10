import { spawn, spawnSync } from "node:child_process";
import path from "node:path";

const script = process.argv[2];
const args = process.argv.slice(3);

if (!script) {
  console.error("usage: node tools/wsl-run.mjs <script> [args...]");
  process.exit(1);
}

function run(command, commandArgs) {
  const child = spawn(command, commandArgs, { stdio: "inherit" });
  child.on("exit", (code, signal) => {
    if (signal) {
      process.kill(process.pid, signal);
      return;
    }
    process.exit(code ?? 1);
  });
}

if (process.platform !== "win32") {
  run("bash", [script, ...args]);
} else {
  const cwd = process.cwd().replaceAll("\\", "/");
  const converted = spawnSync("wsl", ["wslpath", "-a", cwd], {
    encoding: "utf8",
  });
  if (converted.status !== 0) {
    process.stderr.write(converted.stderr || "failed to convert path for WSL\n");
    process.exit(converted.status ?? 1);
  }

  const wslCwd = converted.stdout.trim();
  const wslScript = path.posix.join(wslCwd, script.replaceAll("\\", "/"));
  run("wsl", ["bash", wslScript, ...args]);
}
