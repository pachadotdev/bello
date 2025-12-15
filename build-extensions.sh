#!/usr/bin/env zsh
set -euo pipefail

# Build both Firefox (.xpi) and Chrome (.zip) extension packages.
# If a Chrome/Chromium binary is available, the script will also try to
# pack a .crx using the browser's pack-extension flag (optional).

SRC="connector"
OUT="browser-extensions"
TMP="build/tmp-extension"
rm -rf "$OUT" "$TMP"
mkdir -p "$OUT" "$TMP"

echo "Building browser extensions from: $SRC -> $OUT"

# --- Firefox: build .xpi (web-ext preferred) ---
if command -v web-ext >/dev/null 2>&1; then
  echo "Using web-ext to build Firefox XPI..."
  web-ext build --source-dir "$SRC" --artifacts-dir "$OUT"
  if [ -d "$OUT/web-ext-artifacts" ]; then
    mv "$OUT"/web-ext-artifacts/*.zip "$OUT"/connector.xpi
    rm -rf "$OUT"/web-ext-artifacts
  fi
else
  echo "web-ext not found; creating XPI via zip"
  (cd "$SRC" && zip -r "../$OUT/connector.xpi" . -x '*/.DS_Store' 'node_modules/*')
fi

# --- Chrome: create zip for upload or load-unpacked ---
echo "Creating Chrome ZIP..."
(cd "$SRC" && zip -r "../$OUT/connector-chrome.zip" . -x '*/.DS_Store' 'node_modules/*')

# --- Optional: pack .crx if Chrome/Chromium binary is available ---
# Try common binary names
CHROME_BIN=""
for cmd in google-chrome stable google-chrome-stable chromium chromium-browser chrome; do
  if command -v "$cmd" >/dev/null 2>&1; then
    CHROME_BIN=$(command -v $cmd)
    break
  fi
done

if [ -n "$CHROME_BIN" ]; then
  echo "Found Chrome binary: $CHROME_BIN — attempting to pack CRX (optional)"
  # Copy source to tmp because chrome writes .crx next to the root dir
  rsync -a --exclude 'node_modules' "$SRC/" "$TMP/"
  # Use a generated key if none exists; keep key in OUT to reuse extension ID
  KEY="$OUT/connector.pem"
  if [ ! -f "$KEY" ]; then
    echo "Generating new private key: $KEY"
    openssl genrsa -out "$KEY" 2048 >/dev/null 2>&1 || true
  fi
  # pack-extension flag: output .crx will be created adjacent to TMP dir
  # Some Chrome builds print to stdout; run and capture
  PACK_CMD="$CHROME_BIN --pack-extension=$TMP --pack-extension-key=$KEY"
  echo "> $PACK_CMD"
  if $PACK_CMD >/dev/null 2>&1; then
    # find produced .crx (it will be named tmp.crx or connector.crx depending on chrome)
    CRX_CANDIDATES=("$TMP"/*.crx "$SRC"/*.crx)
    for p in "${CRX_CANDIDATES[@]}"; do
      if [ -f "$p" ]; then
        mv "$p" "$OUT/connector.crx"
        echo "Packed CRX -> $OUT/connector.crx"
        break
      fi
    done
  else
    echo "Packing CRX failed (this is optional). You can still use the ZIP or Load Unpacked."
  fi
else
  echo "Chrome/Chromium binary not found; skipping .crx packing."
fi

echo "Built: $OUT/connector.xpi and $OUT/connector-chrome.zip"
if [ -f "$OUT/connector.crx" ]; then
  echo "Also created: $OUT/connector.crx (may be blocked by Chrome policies)"
fi
