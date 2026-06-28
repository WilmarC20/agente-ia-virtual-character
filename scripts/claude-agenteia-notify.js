#!/usr/bin/env node
// Copia sincronizada con ~/.claude/hooks/agenteia-notify.js
// Instalar: copy scripts\claude-agenteia-notify.js %USERPROFILE%\.claude\hooks\agenteia-notify.js

const http = require("http");
const https = require("https");
const fs = require("fs");
const path = require("path");
const os = require("os");

const BASE = (process.env.AGENTEIA_NOTIFY_URL || "http://127.0.0.1:8000").replace(/\/$/, "");
const DISABLED = process.env.AGENTEIA_NOTIFY_DISABLE === "1";
const TIMEOUT_MS = 8000;
const LOG_PATH = path.join(os.homedir(), ".claude", "hooks", "agenteia-notify.log");

const HANDLED_EVENTS = new Set([
  "PermissionRequest",
  "Notification",
  "PostToolUseFailure",
  "StopFailure",
  "SubagentStop",
  "Elicitation",
  "TaskCompleted",
]);

function log(line) {
  try {
    fs.mkdirSync(path.dirname(LOG_PATH), { recursive: true });
    fs.appendFileSync(LOG_PATH, `[${new Date().toISOString()}] ${line}\n`);
  } catch {
    // logging must never throw
  }
}

function readStdin() {
  return new Promise((resolve) => {
    if (process.stdin.isTTY) return resolve("");
    let buf = "";
    process.stdin.setEncoding("utf8");
    process.stdin.on("data", (chunk) => (buf += chunk));
    process.stdin.on("end", () => resolve(buf));
    process.stdin.on("error", () => resolve(buf));
  });
}

function normPath(p) {
  return String(p || "").replace(/\\/g, "/").replace(/^\/([a-z]:)/i, "$1");
}

function projectName(parsed) {
  const root = normPath(
    process.env.CLAUDE_PROJECT_DIR ||
      process.env.CURSOR_PROJECT_DIR ||
      parsed.cwd ||
      (Array.isArray(parsed.workspace_roots) && parsed.workspace_roots[0]) ||
      ""
  );
  if (root) return path.basename(root);
  return "";
}

function fileName(parsed) {
  const input = parsed.tool_input || {};
  for (const key of ["path", "target_file", "file_path", "file", "pattern"]) {
    if (typeof input[key] === "string" && input[key].trim()) {
      return path.basename(normPath(input[key]));
    }
  }
  if (typeof parsed.file_path === "string" && parsed.file_path.trim()) {
    return path.basename(normPath(parsed.file_path));
  }
  return "";
}

function planForEvent(event, parsed) {
  switch (event) {
    case "Notification":
      return { kind: "ask_question", speak: true, priority: true };
    case "PermissionRequest":
      return { kind: "approval_needed", speak: true, priority: true };
    case "PostToolUseFailure":
      return { kind: "agent_blocked", speak: true, priority: true };
    case "StopFailure":
      return { kind: "stop_failure", speak: true, priority: true };
    case "SubagentStop":
      return { kind: "subagent_done", speak: true, priority: false };
    case "Elicitation":
      return { kind: "elicitation", speak: true, priority: true };
    case "TaskCompleted":
      return { kind: "task_completed", speak: true, priority: false };
    default:
      return null;
  }
}

function buildContext(event, parsed) {
  const ctx = {
    source: "claude",
    client: "Claude Code",
  };
  if (event) ctx.event = event;

  const project = projectName(parsed);
  const file = fileName(parsed);
  const tool = parsed.tool_name ? String(parsed.tool_name).slice(0, 80) : "";
  if (project) ctx.project = project;
  if (file) ctx.file = file;
  if (tool) ctx.tool = tool;

  const err = parsed.error || parsed.error_message || parsed.error_type;
  if (err) ctx.error = String(err).slice(0, 120);

  const task = parsed.task_name || parsed.task_id;
  if (task) ctx.task = String(task).slice(0, 80);

  const agentType = parsed.agent_type || parsed.agent_id;
  if (agentType) ctx.label = String(agentType).slice(0, 80);

  const server = parsed.server_name;
  if (server) ctx.server = String(server).slice(0, 80);

  if (event === "Notification" && parsed.message) {
    ctx.hint = String(parsed.message).slice(0, 100);
  }

  return ctx;
}

function postNotify(payload) {
  return new Promise((resolve) => {
    const body = JSON.stringify(payload);
    const url = new URL(`${BASE}/api/dev/notify`);
    const lib = url.protocol === "https:" ? https : http;
    const req = lib.request(
      {
        hostname: url.hostname,
        port: url.port || (url.protocol === "https:" ? 443 : 80),
        path: url.pathname,
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Content-Length": Buffer.byteLength(body),
        },
        timeout: TIMEOUT_MS,
      },
      (res) => {
        let data = "";
        res.on("data", (chunk) => {
          if (data.length < 4096) data += chunk.toString();
        });
        res.on("end", () => resolve({ status: res.statusCode, body: data }));
      }
    );
    req.on("timeout", () => {
      req.destroy();
      resolve({ status: 0, body: "timeout" });
    });
    req.on("error", (err) => resolve({ status: 0, body: err.message }));
    req.write(body);
    req.end();
  });
}

(async () => {
  if (DISABLED) {
    log("disabled via AGENTEIA_NOTIFY_DISABLE=1");
    process.exit(0);
  }

  const raw = await readStdin();
  let parsed = {};
  try {
    parsed = JSON.parse(raw || "{}");
  } catch (err) {
    log(`stdin not JSON: ${err.message}`);
    process.exit(0);
  }

  const event = parsed.hook_event_name || "unknown";
  if (!HANDLED_EVENTS.has(event)) {
    log(`skip unhandled event=${event}`);
    process.exit(0);
  }

  const plan = planForEvent(event, parsed);
  if (!plan) {
    log(`skip no plan event=${event}`);
    process.exit(0);
  }

  const context = buildContext(event, parsed);
  const dedupeParts = [event, plan.kind, context.project || "", context.file || "", context.tool || "", context.task || ""];
  const payload = {
    kind: plan.kind,
    context,
    speak: plan.speak,
    priority: plan.priority,
    dedupe_key: dedupeParts.join(":"),
  };

  log(`fire event=${event} kind=${plan.kind} ctx=${JSON.stringify(context)}`);
  const res = await postNotify(payload);
  log(`notify status=${res.status} body=${String(res.body).slice(0, 300).replace(/\s+/g, " ")}`);
  process.exit(0);
})();
