import { execFileSync } from "node:child_process";

const output = execFileSync("wsl", [
  "bash",
  "/mnt/c/Users/bunny/Downloads/braindoom/tools/elvm-doom-random.sh"
]);

const expected = Buffer.from([8, 109, 220, 222, 241, 149, 107, 75]);

if (!output.subarray(0, expected.length).equals(expected)) {
  throw new Error(`Unexpected Doom random bytes: ${output.toString("hex")}`);
}

console.log("Doom m_random Brainfuck harness passed");
