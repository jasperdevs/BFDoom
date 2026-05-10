import { createHash } from "node:crypto";
import { spawn, spawnSync } from "node:child_process";
import { existsSync, mkdirSync } from "node:fs";
import http from "node:http";
import path from "node:path";

const ROOT = process.cwd();
const WIDTH = 640;
const HEIGHT = 400;
const WINDOW_WIDTH = 854;
const WINDOW_HEIGHT = 560;
const FRAME_BYTES = WIDTH * HEIGHT * 3;
const PACKET_BYTES = 16 + FRAME_BYTES;

const clients = new Set();
let lastFrame = null;

function wslScriptPath(script) {
  if (process.platform !== "win32") {
    return script;
  }
  const converted = spawnSync("wsl", ["wslpath", "-a", ROOT.replaceAll("\\", "/")], {
    encoding: "utf8",
  });
  if (converted.status !== 0) {
    throw new Error(converted.stderr || "failed to convert path for WSL");
  }
  return path.posix.join(converted.stdout.trim(), script.replaceAll("\\", "/"));
}

function launchRunner() {
  if (process.platform === "win32") {
    return spawn("wsl", ["bash", wslScriptPath("tools/run-bfdoom-window.sh")], {
      stdio: ["pipe", "pipe", "pipe"],
    });
  }
  return spawn("bash", ["tools/run-bfdoom-window.sh"], {
    stdio: ["pipe", "pipe", "pipe"],
  });
}

function encodeWsFrame(payload) {
  const data = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  if (data.length < 126) {
    return Buffer.concat([Buffer.from([0x82, data.length]), data]);
  }
  if (data.length <= 0xffff) {
    const header = Buffer.alloc(4);
    header[0] = 0x82;
    header[1] = 126;
    header.writeUInt16BE(data.length, 2);
    return Buffer.concat([header, data]);
  }
  const header = Buffer.alloc(10);
  header[0] = 0x82;
  header[1] = 127;
  header.writeBigUInt64BE(BigInt(data.length), 2);
  return Buffer.concat([header, data]);
}

function sendFrame(socket, frame) {
  if (socket.destroyed) {
    return;
  }
  socket.write(encodeWsFrame(frame));
}

function broadcastFrame(frame) {
  lastFrame = frame;
  for (const socket of clients) {
    sendFrame(socket, frame);
  }
}

function decodeClientFrames(socket, chunk, runner) {
  socket._wsBuffer = socket._wsBuffer ? Buffer.concat([socket._wsBuffer, chunk]) : chunk;
  let offset = 0;

  while (socket._wsBuffer.length - offset >= 2) {
    const start = offset;
    const second = socket._wsBuffer[offset + 1];
    const masked = (second & 0x80) !== 0;
    let length = second & 0x7f;
    offset += 2;

    if (length === 126) {
      if (socket._wsBuffer.length - offset < 2) {
        offset = start;
        break;
      }
      length = socket._wsBuffer.readUInt16BE(offset);
      offset += 2;
    } else if (length === 127) {
      if (socket._wsBuffer.length - offset < 8) {
        offset = start;
        break;
      }
      length = Number(socket._wsBuffer.readBigUInt64BE(offset));
      offset += 8;
    }

    if (!masked || socket._wsBuffer.length - offset < 4 + length) {
      offset = start;
      break;
    }

    const mask = socket._wsBuffer.subarray(offset, offset + 4);
    offset += 4;
    const payload = Buffer.alloc(length);
    for (let i = 0; i < length; i++) {
      payload[i] = socket._wsBuffer[offset + i] ^ mask[i & 3];
    }
    offset += length;

    const text = payload.toString("utf8");
    if (text.length > 0 && runner.stdin.writable) {
      runner.stdin.write(text);
    }
  }

  socket._wsBuffer = socket._wsBuffer.subarray(offset);
}

function page() {
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>BFDoom</title>
  <style>
    html, body {
      margin: 0;
      width: 100%;
      height: 100%;
      overflow: hidden;
      background: #050505;
      color: #f5f5f5;
      font-family: ui-monospace, SFMono-Regular, Consolas, monospace;
    }
    body {
      display: grid;
      place-items: start center;
    }
    canvas {
      width: min(100vw, ${WINDOW_WIDTH}px, calc((100vh - 24px) * 1.6));
      height: min(calc(100vh - 24px), ${Math.round(WINDOW_WIDTH / 1.6)}px, calc(100vw / 1.6));
      image-rendering: pixelated;
      background: #000;
      outline: 1px solid #222;
    }
    #status {
      position: fixed;
      left: 12px;
      bottom: 10px;
      padding: 4px 7px;
      background: rgba(0, 0, 0, 0.65);
      color: #fff;
      font-size: 12px;
      pointer-events: none;
    }
  </style>
</head>
<body>
  <canvas id="screen" width="${WIDTH}" height="${HEIGHT}"></canvas>
  <div id="status">BFDoom window mode</div>
  <script>
    const canvas = document.getElementById("screen");
    const ctx = canvas.getContext("2d", { alpha: false });
    const status = document.getElementById("status");
    const image = ctx.createImageData(${WIDTH}, ${HEIGHT});
    const keys = new Set();
    let socket;

    function draw(rgb) {
      const out = image.data;
      for (let i = 0, j = 0; i < rgb.length; i += 3, j += 4) {
        out[j] = rgb[i];
        out[j + 1] = rgb[i + 1];
        out[j + 2] = rgb[i + 2];
        out[j + 3] = 255;
      }
      ctx.putImageData(image, 0, 0);
    }

    function mapKey(event) {
      const key = event.key.toLowerCase();
      if (key === "arrowup") return "w";
      if (key === "arrowdown") return "s";
      if (key === "arrowleft") return "a";
      if (key === "arrowright") return "d";
      if (key === " ") return "f";
      if (key === "escape") return "q";
      if ("wasdfqe1234567".includes(key)) return key;
      return "";
    }

    addEventListener("keydown", (event) => {
      const key = mapKey(event);
      if (!key) return;
      event.preventDefault();
      keys.add(key);
      if ("fqe1234567".includes(key) && socket?.readyState === WebSocket.OPEN) {
        socket.send(key);
      }
    });

    addEventListener("keyup", (event) => {
      const key = mapKey(event);
      if (!key) return;
      event.preventDefault();
      keys.delete(key);
    });

    function sendHeldKeys() {
      if (socket?.readyState !== WebSocket.OPEN) return;
      let input = "";
      if (keys.has("w")) input += "w";
      if (keys.has("s")) input += "s";
      if (keys.has("a")) input += "a";
      if (keys.has("d")) input += "d";
      if (input) socket.send(input);
    }

    function connect() {
      socket = new WebSocket("ws://" + location.host + "/input");
      socket.binaryType = "arraybuffer";
      socket.onopen = () => {
        status.textContent = "WASD/arrows move, F/Space fire, E use, Q/Esc quit";
      };
      socket.onmessage = (event) => draw(new Uint8Array(event.data));
      socket.onclose = () => {
        status.textContent = "BFDoom stopped";
        setTimeout(connect, 700);
      };
    }

    setInterval(sendHeldKeys, 16);
    canvas.focus();
    connect();
  </script>
</body>
</html>`;
}

function launchBrowser(url) {
  if (process.platform !== "win32") {
    spawn("xdg-open", [url], { detached: true, stdio: "ignore" }).unref();
    return;
  }
  const candidates = [
    `${process.env.ProgramFiles}\\Microsoft\\Edge\\Application\\msedge.exe`,
    `${process.env["ProgramFiles(x86)"]}\\Microsoft\\Edge\\Application\\msedge.exe`,
    `${process.env.LOCALAPPDATA}\\Microsoft\\Edge\\Application\\msedge.exe`,
    `${process.env.ProgramFiles}\\Google\\Chrome\\Application\\chrome.exe`,
    `${process.env["ProgramFiles(x86)"]}\\Google\\Chrome\\Application\\chrome.exe`,
    `${process.env.LOCALAPPDATA}\\Google\\Chrome\\Application\\chrome.exe`,
    `${process.env.LOCALAPPDATA}\\imput\\Helium\\Application\\chrome.exe`,
  ].filter(Boolean);

  const browser = candidates.find((candidate) => existsSync(candidate));

  if (browser) {
    const profileDir = path.join(ROOT, "build", "bfdoom-window-profile");
    mkdirSync(profileDir, { recursive: true });
    spawn(browser, [
      `--app=${url}`,
      `--window-size=${WINDOW_WIDTH},${WINDOW_HEIGHT}`,
      `--user-data-dir=${profileDir}`,
      "--force-device-scale-factor=1",
    ], {
      detached: true,
      stdio: "ignore",
    }).unref();
    return;
  }

  spawn("cmd", ["/c", "start", "", url], { detached: true, stdio: "ignore" }).unref();
}

const runner = launchRunner();
let parserBuffer = Buffer.alloc(0);

runner.stdout.on("data", (chunk) => {
  parserBuffer = Buffer.concat([parserBuffer, chunk]);
  while (parserBuffer.length >= PACKET_BYTES) {
    const magic = parserBuffer.subarray(0, 4).toString("ascii");
    if (magic !== "BFDW") {
      const next = parserBuffer.indexOf("BFDW", 1, "ascii");
      parserBuffer = next >= 0 ? parserBuffer.subarray(next) : Buffer.alloc(0);
      return;
    }
    const width = parserBuffer.readUInt16LE(4);
    const height = parserBuffer.readUInt16LE(6);
    const length = parserBuffer.readUInt32LE(12);
    const packetBytes = 16 + length;
    if (parserBuffer.length < packetBytes) {
      break;
    }
    if (width === WIDTH && height === HEIGHT && length === FRAME_BYTES) {
      broadcastFrame(parserBuffer.subarray(16, packetBytes));
    }
    parserBuffer = parserBuffer.subarray(packetBytes);
  }
});

runner.stderr.on("data", (chunk) => {
  process.stderr.write(chunk);
});

runner.on("exit", (code, signal) => {
  for (const socket of clients) {
    socket.destroy();
  }
  server.close();
  if (signal) {
    process.kill(process.pid, signal);
    return;
  }
  process.exit(code ?? 0);
});

const server = http.createServer((request, response) => {
  if (request.url === "/" || request.url === "/index.html") {
    response.writeHead(200, { "content-type": "text/html; charset=utf-8" });
    response.end(page());
    return;
  }
  response.writeHead(404);
  response.end("not found");
});

server.on("upgrade", (request, socket) => {
  if (request.url !== "/input") {
    socket.destroy();
    return;
  }
  const key = request.headers["sec-websocket-key"];
  const accept = createHash("sha1")
    .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
    .digest("base64");
  socket.write(
    "HTTP/1.1 101 Switching Protocols\r\n" +
      "Upgrade: websocket\r\n" +
      "Connection: Upgrade\r\n" +
      `Sec-WebSocket-Accept: ${accept}\r\n\r\n`,
  );
  clients.add(socket);
  socket.on("data", (chunk) => decodeClientFrames(socket, chunk, runner));
  socket.on("close", () => clients.delete(socket));
  socket.on("error", () => clients.delete(socket));
  if (lastFrame) {
    sendFrame(socket, lastFrame);
  }
});

server.listen(0, "127.0.0.1", () => {
  const address = server.address();
  const url = `http://127.0.0.1:${address.port}/`;
  console.log(`BFDoom window: ${url}`);
  launchBrowser(url);
});
