#!/usr/bin/env python3
"""
clops_monitor.py — CamClops job queue live monitor.

Serves a status page on the local network viewable from any browser
(Safari, Firefox, iPhone, iPad, desktop).

Usage:
    python3 scripts/clops_monitor.py [--port 8647]

Firewall (Alma 9 / firewalld — temporary until next reboot):
    firewall-cmd --add-port=8647/tcp --temporary

Then browse to http://penny:8647 from any device on the LAN.

────────────────────────────────────────────────────────────────────────────────
camclops-gui side (not in this script):

camclops-gui writes /tmp/camclops_jobs.json whenever the queue changes.
Use a 250 ms debounce timer so rapid progress signals don't thrash disk.

JSON schema:
{
  "updated": "2026-05-09T14:32:11",
  "jobs": [
    {
      "desc":   "Collage — VP:8F",
      "state":  "running",
      "pct":    42,
      "status": "Left concat  42%",
      "steps": [
        { "name": "Front concat", "state": "done",    "pct": 100 },
        { "name": "Left concat",  "state": "running", "pct": 42  },
        { "name": "4K collage",   "state": "waiting", "pct": -1  }
      ]
    }
  ]
}
────────────────────────────────────────────────────────────────────────────────
"""

import argparse
import base64
import json
import socket
import sys
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

STATUS_FILE  = Path("/tmp/camclops_jobs.json")
PORT_DEFAULT = 8647

# ─── Logo (optional watermark) ────────────────────────────────────────────────
# Look for the Nutball-Labs logo alongside the script's resources directory.
# If not found the page still works — the watermark is purely cosmetic.

def _load_logo_b64() -> str:
    script_dir = Path(__file__).resolve().parent
    candidates = [
        script_dir / "../gui/resources/Nutball-Labs_logo.png",
        script_dir / "resources/Nutball-Labs_logo.png",
        script_dir / "Nutball-Labs_logo.png",
    ]
    for p in candidates:
        try:
            data = p.resolve().read_bytes()
            return base64.b64encode(data).decode("ascii")
        except (OSError, FileNotFoundError):
            continue
    return ""

_LOGO_B64 = _load_logo_b64()
_LOGO_CSS = (
    f"background-image: url('data:image/png;base64,{_LOGO_B64}');"
    "background-repeat: no-repeat;"
    "background-position: center center;"
    "background-attachment: fixed;"
    "background-size: 40% auto;"
    "background-blend-mode: normal;"
) if _LOGO_B64 else ""

# ─── Embedded page ────────────────────────────────────────────────────────────

_HTML = f"""\
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="theme-color" content="#2c3e50">
  <title>CamClops — Job Queue</title>
  <style>
    *, *::before, *::after {{ box-sizing: border-box; margin: 0; padding: 0; }}

    body {{
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto,
                   "Helvetica Neue", sans-serif;
      background: #eef0f4;
      color: #1a2030;
      min-height: 100dvh;
    }}

    /* ── Header ── */
    header {{
      background: #2c3e50;
      color: #fff;
      padding: 14px 18px;
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      position: sticky;
      top: 0;
      z-index: 10;
      box-shadow: 0 2px 6px rgba(0,0,0,.25);
    }}
    header h1  {{ font-size: 1.05rem; font-weight: 600; letter-spacing: .03em; }}
    #clock     {{ font-size: .72rem; color: #8fa8bf; font-variant-numeric: tabular-nums; }}

    /* ── Content area ── */
    #container {{
      padding: 16px;
      max-width: 720px;
      margin: 0 auto;
      {_LOGO_CSS}
      opacity-for-content: 1;
    }}
    /* Pseudo-watermark: content sits on top at full opacity, logo bleeds through at low opacity */
    #container::before {{
      content: '';
      display: block;
      position: fixed;
      inset: 0;
      pointer-events: none;
      {_LOGO_CSS}
      opacity: 0.05;
      z-index: 0;
    }}
    #container > * {{ position: relative; z-index: 1; }}

    .empty {{
      text-align: center;
      padding: 56px 24px;
      color: #8090a8;
      font-size: .95rem;
    }}
    .empty span {{ display: block; font-size: 2.5rem; margin-bottom: 12px; opacity: .4; }}

    /* ── Job card ── */
    .job {{
      background: rgba(255,255,255,0.92);
      border-radius: 8px;
      padding: 0;
      margin-bottom: 10px;
      box-shadow: 0 1px 3px rgba(0,0,0,.09);
      overflow: hidden;
    }}

    /* Header row */
    .job-header {{
      display: grid;
      grid-template-columns: 13px 1fr auto auto;
      column-gap: 10px;
      align-items: center;
      padding: 11px 12px 9px;
    }}
    .dot {{
      width: 13px; height: 13px; border-radius: 50%; flex-shrink: 0;
    }}
    .dot.queued    {{ background: #6c757d; }}
    .dot.running   {{ background: #fd7e14; animation: blink 1.4s ease-in-out infinite; }}
    .dot.done      {{ background: #28a745; }}
    .dot.failed    {{ background: #dc3545; }}
    .dot.cancelled {{ background: #adb5bd; }}
    @keyframes blink {{ 0%,100%{{opacity:1}} 50%{{opacity:.35}} }}

    .job-text {{ min-width: 0; }}
    .job-desc {{
      font-size: .91rem; font-weight: 500; line-height: 1.35;
      white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
    }}
    .job-status {{
      margin-top: 2px;
      font-size: .73rem; color: #677080;
    }}

    /* Elapsed + expand toggle area */
    .job-meta {{
      display: flex; align-items: center; gap: 8px; flex-shrink: 0;
    }}
    .job-elapsed {{
      font-size: .70rem; color: #8090a8;
      font-variant-numeric: tabular-nums;
      min-width: 32px; text-align: right;
    }}
    .expand-btn {{
      background: none; border: none; cursor: pointer;
      color: #8090a8; font-size: .70rem; padding: 0 2px;
      line-height: 1;
    }}
    .expand-btn:hover {{ color: #333; }}

    /* Overall progress bar — sits below the header row */
    .job-bar-row {{
      padding: 0 12px 9px;
    }}
    .bar-track {{
      height: 5px; background: #e2e5ea; border-radius: 3px; overflow: hidden;
    }}
    .bar {{
      height: 100%; border-radius: 3px; background: #fd7e14;
      transition: width .35s ease; will-change: width;
    }}
    .bar.done      {{ background: #28a745; }}
    .bar.failed    {{ background: #dc3545; }}
    .bar.cancelled {{ background: #adb5bd; }}
    .bar.indet     {{
      width: 38% !important;
      animation: slide 1.3s linear infinite;
    }}
    @keyframes slide {{ from{{margin-left:-38%}} to{{margin-left:100%}} }}

    /* ── Steps area ── */
    .steps {{
      border-top: 1px solid #e8eaed;
      padding: 6px 12px 8px 28px;
    }}
    .step {{
      display: grid;
      grid-template-columns: 9px 1fr 90px 72px;
      column-gap: 8px;
      align-items: center;
      padding: 4px 0;
    }}
    .step-dot {{
      width: 9px; height: 9px; border-radius: 50%;
    }}
    .step-dot.waiting  {{ background: #9aa0a8; }}
    .step-dot.running  {{ background: #fd7e14; animation: blink 1.4s ease-in-out infinite; }}
    .step-dot.done     {{ background: #28a745; }}
    .step-dot.failed   {{ background: #dc3545; }}

    .step-name {{
      font-size: .78rem; color: #333;
      white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
    }}
    .step-bar-track {{
      height: 6px; background: #e2e5ea; border-radius: 3px; overflow: hidden;
    }}
    .step-bar {{
      height: 100%; border-radius: 3px; background: #fd7e14;
      transition: width .3s ease;
    }}
    .step-bar.done     {{ background: #28a745; }}
    .step-bar.failed   {{ background: #dc3545; }}
    .step-bar.waiting  {{ background: #9aa0a8; width: 0 !important; }}
    .step-bar.indet    {{ width: 38% !important; animation: slide 1.3s linear infinite; }}

    .step-status {{
      font-size: .68rem; color: #677080; text-align: right;
      font-variant-numeric: tabular-nums;
    }}
    .step-status.done   {{ color: #28a745; }}
    .step-status.failed {{ color: #dc3545; }}

    /* ── Footer ── */
    #footer {{
      text-align: center; font-size: .70rem; color: #aab0bc;
      padding: 8px 0 24px;
    }}

    @media (max-width: 420px) {{
      header h1 {{ font-size: .95rem; }}
      .job-desc {{ font-size: .86rem; }}
      .step {{ grid-template-columns: 9px 1fr 70px 60px; }}
    }}
  </style>
</head>
<body>
  <header>
    <h1>CamClops &mdash; Job Queue</h1>
    <span id="clock">connecting&hellip;</span>
  </header>

  <div id="container"><div class="empty"><span>&#x231B;</span>Waiting for data&hellip;</div></div>
  <div id="footer"></div>

  <script>
    "use strict";

    // Track which jobs are expanded (keyed by desc string).
    // Defaults to collapsed; user can click to expand.
    const expandedJobs = new Set();

    function esc(s) {{
      return String(s ?? "")
        .replace(/&/g,"&amp;").replace(/</g,"&lt;")
        .replace(/>/g,"&gt;").replace(/"/g,"&quot;");
    }}

    function stepHTML(s) {{
      const state = s.state || "waiting";
      const pct   = Number.isFinite(s.pct) ? s.pct : -1;
      const indet = pct < 0 && state === "running";
      const barW  = indet ? 38 : Math.max(0, Math.min(100, pct));
      const barCls = ["step-bar", state, indet ? "indet" : ""].filter(Boolean).join(" ");
      let statusText = "";
      if (state === "done")    statusText = "Done";
      else if (state === "failed")  statusText = "Failed";
      else if (state === "running" && pct >= 0) statusText = pct + "%";
      else if (state === "waiting") statusText = "Waiting";
      return `<div class="step">
  <div class="step-dot ${{esc(state)}}"></div>
  <div class="step-name" title="${{esc(s.name)}}">${{esc(s.name)}}</div>
  <div class="step-bar-track"><div class="${{barCls}}" style="width:${{barW}}%"></div></div>
  <div class="step-status ${{esc(state)}}">${{esc(statusText)}}</div>
</div>`;
    }}

    function jobHTML(j) {{
      const state  = j.state || "queued";
      const pct    = Number.isFinite(j.pct) ? j.pct : -1;
      const indet  = pct < 0 && state === "running";
      const barW   = indet ? 38 : Math.max(0, Math.min(100, pct));
      const barCls = ["bar", state === "done" ? "done"
                           : state === "failed" ? "failed"
                           : state === "cancelled" ? "cancelled"
                           : "", indet ? "indet" : ""].filter(Boolean).join(" ");
      const steps  = Array.isArray(j.steps) ? j.steps : [];
      const isExpanded = expandedJobs.has(j.desc);
      const hasMeta   = state === "running" || state === "done" || state === "failed";

      let statusLine = "";
      if (j.status) statusLine = esc(j.status);

      let metaHTML = "";
      if (steps.length) {{
        const btn = `<button class="expand-btn" onclick="toggleExpand(${{esc(JSON.stringify(j.desc))}})">${{isExpanded ? "▼" : "▶"}}</button>`;
        metaHTML = `<div class="job-meta">${{btn}}</div>`;
      }}

      let stepsHTML = "";
      if (steps.length && isExpanded) {{
        stepsHTML = `<div class="steps">${{steps.map(stepHTML).join("")}}</div>`;
      }}

      return `<div class="job">
  <div class="job-header">
    <div class="dot ${{esc(state)}}"></div>
    <div class="job-text">
      <div class="job-desc" title="${{esc(j.desc)}}">${{esc(j.desc)}}</div>
      ${{statusLine ? `<div class="job-status">${{statusLine}}</div>` : ""}}
    </div>
    ${{metaHTML}}
  </div>
  <div class="job-bar-row">
    <div class="bar-track"><div class="${{barCls}}" style="width:${{barW}}%"></div></div>
  </div>
  ${{stepsHTML}}
</div>`;
    }}

    function toggleExpand(desc) {{
      if (expandedJobs.has(desc)) expandedJobs.delete(desc);
      else expandedJobs.add(desc);
      // Re-render with current data (cached in lastData)
      if (window._lastData) render(window._lastData);
    }}

    function render(data) {{
      window._lastData = data;
      const box  = document.getElementById("container");
      const jobs = Array.isArray(data.jobs) ? data.jobs : [];
      box.innerHTML = jobs.length
        ? jobs.map(jobHTML).join("")
        : '<div class="empty"><span>&#x2713;</span>Queue is empty.</div>';
      document.getElementById("footer").textContent =
        data.updated ? "camclops-gui last wrote: " + data.updated : "";
      document.getElementById("clock").textContent =
        new Date().toLocaleTimeString([], {{hour:"2-digit",minute:"2-digit",second:"2-digit"}});
    }}

    async function poll() {{
      try {{
        const r = await fetch("/status", {{ cache: "no-store" }});
        if (r.ok) render(await r.json());
      }} catch (_) {{
        document.getElementById("clock").textContent = "offline";
      }}
    }}

    poll();
    setInterval(poll, 2000);
  </script>
</body>
</html>
"""


# ─── HTTP handler ─────────────────────────────────────────────────────────────

class _Handler(BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        pass   # silence per-request console noise

    def _send(self, code, ctype, body: bytes):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]

        if path == "/status":
            data = _read_status()
            self._send(200, "application/json", json.dumps(data).encode())

        elif path in ("/", "/index.html"):
            self._send(200, "text/html; charset=utf-8", _HTML.encode())

        else:
            self._send(404, "text/plain", b"Not found")


def _read_status() -> dict:
    if not STATUS_FILE.exists():
        return {"jobs": [], "updated": None}
    try:
        return json.loads(STATUS_FILE.read_text(encoding="utf-8"))
    except Exception as exc:
        return {"jobs": [], "updated": None, "error": str(exc)}


# ─── Entry point ──────────────────────────────────────────────────────────────

def main():
    global STATUS_FILE
    ap = argparse.ArgumentParser(
        description="CamClops job queue monitor — local-network HTTP status page.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "camclops-gui must write /tmp/camclops_jobs.json (or --status-file path).\n"
            "See the top of this script for the JSON schema.\n"
            "\n"
            "Firewall (Alma 9):\n"
            "  firewall-cmd --add-port=8647/tcp --temporary\n"
        )
    )
    ap.add_argument("--port", type=int, default=PORT_DEFAULT,
                    metavar="N",
                    help=f"port to listen on (default: {PORT_DEFAULT})")
    ap.add_argument("--status-file", default=str(STATUS_FILE),
                    metavar="PATH",
                    help=f"JSON file written by camclops-gui (default: {STATUS_FILE})")
    args = ap.parse_args()

    STATUS_FILE = Path(args.status_file)

    try:
        server = HTTPServer(("0.0.0.0", args.port), _Handler)
    except OSError as exc:
        print(f"Error: cannot bind to port {args.port}: {exc}", file=sys.stderr)
        sys.exit(1)

    hostname = socket.gethostname()
    logo_msg = " (logo loaded)" if _LOGO_B64 else " (logo not found — watermark skipped)"
    print(f"CamClops monitor running.{logo_msg}")
    print(f"  URL:         http://{hostname}:{args.port}")
    print(f"  Status file: {STATUS_FILE}")
    print(f"  Press Ctrl+C to stop.")
    print()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
        server.server_close()


if __name__ == "__main__":
    main()
# SN: 00117
