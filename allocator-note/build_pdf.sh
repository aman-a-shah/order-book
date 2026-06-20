#!/usr/bin/env bash
# regenerate data -> serve (fetch needs HTTP) -> headless print to PDF
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
DIR="allocator-note"
PORT=8014
OUT="$DIR/deterministic-allocator.pdf"

# data.json is the recorded run transcribed in site/pdf.md.

# 2. serve the folder (fetch() will not work over file://)
lsof -ti :"$PORT" 2>/dev/null | xargs kill 2>/dev/null || true
python3 -m http.server "$PORT" --directory "$DIR" >/dev/null 2>&1 &
SRV=$!
CHROME_PID=""
trap 'kill $SRV 2>/dev/null || true; [ -n "$CHROME_PID" ] && kill $CHROME_PID 2>/dev/null || true' EXIT
sleep 1

# 3. locate Chrome / Chromium
CHROME=""
for c in \
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
  "/Applications/Chromium.app/Contents/MacOS/Chromium" \
  "$(command -v google-chrome || true)" \
  "$(command -v chromium || true)"; do
  if [ -n "$c" ] && [ -x "$c" ]; then CHROME="$c"; break; fi
done
if [ -z "$CHROME" ]; then echo "no Chrome/Chromium found" >&2; exit 1; fi

# 4. print to PDF. Some managed Chrome installs spawn a persistent updater and
#    never let the headless process exit, so run it in the background and wait
#    for the PDF file to appear and stop growing, then stop Chrome ourselves.
rm -f "$OUT"
"$CHROME" --headless=new --disable-gpu --no-pdf-header-footer \
  --no-first-run --no-default-browser-check --disable-extensions \
  --disable-background-networking --disable-component-update \
  --disable-features=Translate,OptimizationHints \
  --user-data-dir="$(mktemp -d /tmp/chrome-pdf.XXXXXX)" \
  --virtual-time-budget=15000 --run-all-compositor-stages-before-draw \
  --window-size=760,1100 \
  --print-to-pdf="$OUT" "http://localhost:$PORT/index.html" >/dev/null 2>&1 &
CHROME_PID=$!

# NB: avoid `[ cond ] && cmd` as a bare statement here — under `set -e` a false
# test returns 1 and would abort the script. Use explicit if-blocks.
prev=-1; stable=0
for _ in $(seq 1 60); do
  sz=0
  if [ -f "$OUT" ]; then sz=$(wc -c < "$OUT" 2>/dev/null || echo 0); fi
  if [ "$sz" -gt 1000 ] && [ "$sz" = "$prev" ]; then
    stable=$((stable + 1))
    if [ "$stable" -ge 2 ]; then break; fi
  else
    stable=0
  fi
  prev=$sz
  if ! kill -0 "$CHROME_PID" 2>/dev/null; then break; fi   # chrome exited on its own
  sleep 1
done
kill "$CHROME_PID" 2>/dev/null || true

if [ -f "$OUT" ] && [ "$(wc -c < "$OUT")" -gt 1000 ]; then
  echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
else
  echo "FAILED to write $OUT" >&2; exit 1
fi
