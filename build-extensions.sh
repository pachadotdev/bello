#!/usr/bin/env zsh
set -euo pipefail

SRC_FIREFOX="${SRC_FIREFOX:-connector}"
OUT="browser-extensions"
TMP="build/tmp-extension"
rm -rf "$OUT" "$TMP"
mkdir -p "$OUT" "$TMP"

echo "Building browser extensions:" 
echo "  Firefox source: $SRC_FIREFOX"
echo "  Out dir:        $OUT"

# --- Firefox: build .xpi (web-ext preferred) ---
if [ -d "$SRC_FIREFOX" ]; then
  if command -v web-ext >/dev/null 2>&1; then
    echo "Using web-ext to build Firefox XPI from: $SRC_FIREFOX"
    web-ext build --source-dir "$SRC_FIREFOX" --artifacts-dir "$OUT"
    if [ -d "$OUT/web-ext-artifacts" ]; then
      mv "$OUT"/web-ext-artifacts/*.zip "$OUT"/bello-connector.xpi
      rm -rf "$OUT"/web-ext-artifacts
    fi
  else
    echo "web-ext not found; creating XPI via zip from: $SRC_FIREFOX"
    (cd "$SRC_FIREFOX" && zip -r "../$OUT/bello-connector.xpi" . -x '*/.DS_Store' 'node_modules/*')
  fi
else
  echo "Firefox source directory '$SRC_FIREFOX' not found; skipping XPI."
fi

echo "Built artifacts (if sources existed):"
[ -f "$OUT/bello-connector.xpi" ] && echo "  - $OUT/bello-connector.xpi"
