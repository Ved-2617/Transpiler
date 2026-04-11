#!/usr/bin/env python3
"""
server.py — HTTP server for the XPILE Transpiler project
Bridges the HTML frontend ↔ C transpiler binary

Protocol (matches app.js exactly):
  POST /transpile
    Content-Type: text/plain
    Body:  "<lang>\n<source code>"
    → pipes body to ./transpiler via stdin
    → returns JSON: { tokens, ir, python, c, cpp, java, srclang, errors }

  GET  /            → serves home.html  (landing page)
  GET  /app         → serves app.html   (transpiler UI)
  GET  /*           → serves any static file in this directory
"""

import http.server
import subprocess
import json
import os
import sys
import urllib.parse

# ── CONFIG ───────────────────────────────────────────────────────────────────
PORT         = 8080
BINARY_NAME  = "transpiler.exe" if os.name == "nt" else "transpiler"
BASE_DIR     = os.path.dirname(os.path.abspath(__file__))
TRANSPILER   = os.path.join(BASE_DIR, BINARY_NAME)

VALID_LANGS  = {"python", "c", "cpp", "java", "custom"}

# ── COLOUR HELPERS ────────────────────────────────────────────────────────────
def green(s):  return f"\033[92m{s}\033[0m"
def cyan(s):   return f"\033[96m{s}\033[0m"
def yellow(s): return f"\033[93m{s}\033[0m"
def red(s):    return f"\033[91m{s}\033[0m"
def bold(s):   return f"\033[1m{s}\033[0m"

# ── EMPTY RESPONSE TEMPLATE ───────────────────────────────────────────────────
def empty_response(errors=None):
    return {
        "tokens": [], "ir": [],
        "python": "", "c": "", "cpp": "", "java": "",
        "srclang": "", "errors": errors or []
    }


class XpileHandler(http.server.BaseHTTPRequestHandler):

    # Silence default access log (we print our own)
    def log_message(self, fmt, *args):
        pass

    # ── CORS (needed for browser fetch()) ─────────────────────────────────────
    def _cors(self):
        self.send_header("Access-Control-Allow-Origin",  "*")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    # ── OPTIONS preflight ─────────────────────────────────────────────────────
    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.end_headers()

    # ── GET — static file server ───────────────────────────────────────────────
    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path

        # Route aliases
        if path in ("/", ""):
            path = "/home.html"           # landing page
        elif path == "/app":
            path = "/app.html"            # transpiler UI (no .html needed)

        file_path = os.path.join(BASE_DIR, path.lstrip("/"))

        # Security: block path traversal
        if not os.path.abspath(file_path).startswith(BASE_DIR):
            self.send_error(403, "Forbidden")
            return

        if not os.path.isfile(file_path):
            self.send_error(404, f"Not found: {path}")
            print(f"  {red('404')}  {path}")
            return

        ext  = os.path.splitext(file_path)[1].lower()
        mime = {
            ".html": "text/html; charset=utf-8",
            ".css":  "text/css",
            ".js":   "application/javascript",
            ".json": "application/json",
            ".ico":  "image/x-icon",
            ".png":  "image/png",
            ".svg":  "image/svg+xml",
        }.get(ext, "text/plain")

        with open(file_path, "rb") as f:
            content = f.read()

        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(content)))
        self._cors()
        self.end_headers()
        self.wfile.write(content)
        print(f"  {cyan('GET')}  {path}  →  200")

    # ── POST /transpile — invoke C transpiler binary ───────────────────────────
    def do_POST(self):
        if self.path != "/transpile":
            self.send_error(404, "Only POST /transpile is supported")
            return

        # ── Read request body ──────────────────────────────────────────────────
        try:
            length = int(self.headers.get("Content-Length", 0))
            raw    = self.rfile.read(length)
            body   = raw.decode("utf-8")
        except Exception as e:
            self._json(400, empty_response([f"Could not read request body: {e}"]))
            return

        # ── Handle URL-encoded form submission (legacy fallback) ───────────────
        if body.startswith("source="):
            body = urllib.parse.unquote_plus(body[7:])

        body = body.strip()

        if not body:
            self._json(200, empty_response(["Empty source code submitted."]))
            return

        # ── Validate / normalise the language prefix ───────────────────────────
        # app.js sends:  "<lang>\n<source>"
        lines    = body.split("\n", 1)
        lang_tag = lines[0].strip().lower() if lines else "python"
        source   = lines[1] if len(lines) > 1 else ""

        if lang_tag not in VALID_LANGS:
            # Treat entire body as source, default to python
            print(f"  {yellow('WARN')} unknown lang tag '{lang_tag}', defaulting to python")
            lang_tag = "python"
            source   = body         # whole body is source

        # Re-assemble in the exact format the C transpiler expects:
        #   Line 1: language tag
        #   Rest  : source code
        transpiler_input = f"{lang_tag}\n{source}"

        print(f"  {yellow('POST')} /transpile  lang={lang_tag}  src={len(source)} chars")

        # ── Check binary exists ────────────────────────────────────────────────
        if not os.path.isfile(TRANSPILER):
            msg = (f"Transpiler binary not found: {TRANSPILER}\n"
                   f"Compile it first:\n  gcc transpiler.c -o transpiler")
            print(f"  {red('ERROR')} {msg}")
            self._json(200, empty_response([msg]))
            return

        # ── Run transpiler ─────────────────────────────────────────────────────
        try:
            result = subprocess.run(
                [TRANSPILER],
                input=transpiler_input.encode("utf-8"),
                capture_output=True,
                timeout=10
            )

            stdout_raw = result.stdout.decode("utf-8", errors="replace").strip()
            stderr_raw = result.stderr.decode("utf-8", errors="replace").strip()

            # Non-zero exit or empty stdout → return error
            if result.returncode != 0 or not stdout_raw:
                err = stderr_raw or f"Transpiler exited with code {result.returncode} and no output."
                print(f"  {red('ERROR')} transpiler exited {result.returncode}")
                self._json(200, empty_response([err]))
                return

            # ── Parse JSON from transpiler stdout ──────────────────────────────
            data = json.loads(stdout_raw)

            # Ensure all expected keys exist (defensive defaults)
            for key in ("tokens", "ir", "errors"):
                data.setdefault(key, [])
            for key in ("python", "c", "cpp", "java", "srclang"):
                data.setdefault(key, "")

            # Log summary
            n_tok = len(data["tokens"])
            n_ir  = len(data["ir"])
            n_err = len(data["errors"])
            print(f"  {green('OK')}   tokens={n_tok}  IR={n_ir}  errors={n_err}")
            if stderr_raw:
                print(f"  {yellow('WARN')} stderr: {stderr_raw[:160]}")

            self._json(200, data)

        except subprocess.TimeoutExpired:
            msg = "Transpiler timed out (>10 s). Check for infinite loops in your code."
            print(f"  {red('TIMEOUT')} {msg}")
            self._json(200, empty_response([msg]))

        except json.JSONDecodeError as e:
            snippet = stdout_raw[:300] if stdout_raw else "(no output)"
            msg = f"Transpiler returned invalid JSON: {e}\nRaw output: {snippet}"
            print(f"  {red('JSON ERR')} {e}")
            self._json(200, empty_response([msg]))

        except Exception as e:
            print(f"  {red('EXCEPTION')} {e}")
            self._json(500, empty_response([f"Server error: {e}"]))

    # ── Helper: serialise and send a JSON response ─────────────────────────────
    def _json(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self._cors()
        self.end_headers()
        self.wfile.write(body)


# ── ENTRY POINT ───────────────────────────────────────────────────────────────
def main():
    if not os.path.isfile(TRANSPILER):
        print(red(f"\n[ERROR] Transpiler binary not found: {TRANSPILER}"))
        print(yellow("  Compile it first:  gcc transpiler.c -o transpiler\n"))
        sys.exit(1)

    print(bold("\n╔══════════════════════════════════════════╗"))
    print(bold("║        XPILE — Full-Stack Server         ║"))
    print(bold("╚══════════════════════════════════════════╝"))
    print(f"\n  {green('✓')} Transpiler : {TRANSPILER}")
    print(f"  {green('✓')} Static dir : {BASE_DIR}")
    print(f"  {green('✓')} Listening  : {cyan(f'http://localhost:{PORT}')}")
    print(f"\n  {yellow('Landing page →')} {bold(f'http://localhost:{PORT}/')}")
    print(f"  {yellow('Transpiler  →')} {bold(f'http://localhost:{PORT}/app.html')}")
    print(f"\n  Press {bold('Ctrl+C')} to stop.\n")
    print("─" * 44)

    server = http.server.HTTPServer(("", PORT), XpileHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print(f"\n\n  {yellow('Stopped.')} Goodbye!\n")
        server.server_close()


if __name__ == "__main__":
    main()
