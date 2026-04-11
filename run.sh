#!/bin/bash
# ╔══════════════════════════════════════════════════╗
# ║   Mini Transpiler — One-Command Launcher         ║
# ║   Usage: bash run.sh                             ║
# ╚══════════════════════════════════════════════════╝

set -e   # exit immediately if any command fails

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo ""
echo "╔══════════════════════════════════════════════╗"
echo "║     Mini Transpiler — Build & Run            ║"
echo "╚══════════════════════════════════════════════╝"
echo ""

# ── Step 1: Compile the C transpiler engine ──────────
echo "  [1/2] Compiling transpiler.c  ..."
gcc transpiler.c -o transpiler -Wall
echo "        ✓  Binary ready: ./transpiler"
echo ""

# ── Step 2: Start the Python HTTP server ─────────────
echo "  [2/2] Starting server on http://localhost:8080"
echo ""
python3 server.py
