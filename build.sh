#!/usr/bin/env bash
set -euo pipefail

# build.sh - Download DuckDB (if missing), configure, build and run bello
# Usage: ./build.sh [--no-run] [--duckdb-url URL] [--clean] [--help] [--install]

rm -rf ~/.local/share/bello
rm -rf build

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$SCRIPT_DIR"
BUILD_DIR="$REPO_DIR/build"
DUCKDB_DIR="$REPO_DIR/duckdb"
DUCKDB_ZIP_URL="https://github.com/duckdb/duckdb/releases/download/v1.4.3/libduckdb-linux-amd64.zip"
NO_RUN=0
CLEAN_BUILD=0
INSTALL=0

usage(){
	cat <<EOF
Usage: $0 [--no-run] [--duckdb-url URL] [--clean] [--help]

Options:
	--no-run         Configure and build, but do not start the GUI afterward.
	--duckdb-url URL Use a different DuckDB prebuilt zip URL.
	--clean          Force a clean rebuild by removing the build directory.
	--help           Show this help and exit.

This script will:
	- Download and extract a DuckDB prebuilt into "${DUCKDB_DIR}" if that folder
		does not exist.
	- Configure CMake pointing at the extracted headers/library (incremental by default).
	- Build the project (parallel using nproc).
	- Run the resulting binary (unless --no-run is provided).

By default, builds are incremental - only changed files are recompiled.
Use --clean to force a complete rebuild.

EOF
}

while [[ ${#} -gt 0 ]]; do
	case "$1" in
		--no-run) NO_RUN=1; shift ;;
		--duckdb-url) DUCKDB_ZIP_URL="$2"; shift 2 ;;
		--clean) CLEAN_BUILD=1; shift ;;
		--install) INSTALL=1; shift ;;
		--help) usage; exit 0 ;;
		-h) usage; exit 0 ;;
		*) echo "Unknown option: $1"; usage; exit 2 ;;
	esac
done

check_cmd(){
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Warning: required command '$1' not found in PATH" >&2
		return 1
	fi
}

check_cmd cmake || true
check_cmd unzip || check_cmd bsdtar || echo "(install 'unzip' or 'bsdtar' to extract the DuckDB archive)"
check_cmd wget || check_cmd curl || echo "(install 'wget' or 'curl' to download the DuckDB archive)"

mkdir -p "$REPO_DIR"

# If duckdb/ not present, download the prebuilt and extract it there
if [[ ! -d "$DUCKDB_DIR" ]]; then
	echo "duckdb/ not found — downloading prebuilt from: $DUCKDB_ZIP_URL"
	TMPZIP="$REPO_DIR/duckdb.zip"
	if command -v wget >/dev/null 2>&1; then
		wget -q -O "$TMPZIP" "$DUCKDB_ZIP_URL"
	else
		curl -sL -o "$TMPZIP" "$DUCKDB_ZIP_URL"
	fi
	mkdir -p "$DUCKDB_DIR"
	if command -v unzip >/dev/null 2>&1; then
		unzip -q "$TMPZIP" -d "$DUCKDB_DIR"
	else
		bsdtar -xf "$TMPZIP" -C "$DUCKDB_DIR"
	fi
	rm -f "$TMPZIP"
	echo "Extracted DuckDB into $DUCKDB_DIR"
else
	echo "Using existing in-repo duckdb/ at $DUCKDB_DIR"
fi

# Decide which library file to pass to CMake
if [[ -f "$DUCKDB_DIR/libduckdb.so" ]]; then
	DUCKDB_LIB_FILE="$DUCKDB_DIR/libduckdb.so"
elif [[ -f "$DUCKDB_DIR/libduckdb_static.a" ]]; then
	DUCKDB_LIB_FILE="$DUCKDB_DIR/libduckdb_static.a"
else
	echo "Error: no libduckdb.so or libduckdb_static.a found in $DUCKDB_DIR" >&2
	exit 1
fi

echo "Configuring build (DUCKDB include: $DUCKDB_DIR, lib: $DUCKDB_LIB_FILE)"

# Only remove build directory if CMakeCache.txt doesn't exist or if forced
if [[ $CLEAN_BUILD -eq 1 ]]; then
	echo "Clean build requested, removing build directory"
	rm -rf "$BUILD_DIR"
elif [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
	echo "No existing build found, creating fresh build directory"
	rm -rf "$BUILD_DIR"
	mkdir -p "$BUILD_DIR"
else
	echo "Using existing build directory for incremental build"
fi

cmake -S "$REPO_DIR" -B "$BUILD_DIR" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DDUCKDB_INCLUDE_DIRS="$DUCKDB_DIR" \
	-DDUCKDB_LIBRARIES="$DUCKDB_LIB_FILE"

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo "Build finished. Binary should be at: $BUILD_DIR/bello"

if [[ $INSTALL -eq 1 ]]; then
	DEST="/opt/bello"
	BIN_SRC="$BUILD_DIR/bello"
	LOGO_SRC="$REPO_DIR/logo.svg"
	echo "Installing Bello to $DEST"
	if [[ $EUID -ne 0 ]]; then
		echo "Creating $DEST (may prompt for sudo)"
		sudo mkdir -p "$DEST/bin"
		echo "Copying binary to $DEST/bin"
		sudo cp -v "$BIN_SRC" "$DEST/bin/bello"
		sudo chmod 755 "$DEST/bin/bello"
		if [[ -d "$BUILD_DIR/duckdb" ]]; then
			sudo rm -rf "$DEST/duckdb" || true
			sudo cp -r "$BUILD_DIR/duckdb" "$DEST/duckdb"
		fi
		if [[ -f "$LOGO_SRC" ]]; then
			sudo cp -v "$LOGO_SRC" "$DEST/logo.svg"
		fi
	else
		mkdir -p "$DEST/bin"
		cp -v "$BIN_SRC" "$DEST/bin/bello"
		chmod 755 "$DEST/bin/bello"
		if [[ -d "$BUILD_DIR/duckdb" ]]; then
			rm -rf "$DEST/duckdb" || true
			cp -r "$BUILD_DIR/duckdb" "$DEST/duckdb"
		fi
		if [[ -f "$LOGO_SRC" ]]; then
			cp -v "$LOGO_SRC" "$DEST/logo.svg"
		fi
	fi

	DESKTOP_DIR="$HOME/.local/share/applications"
	mkdir -p "$DESKTOP_DIR"
	DESKTOP_FILE="$DESKTOP_DIR/bello.desktop"
	cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Name=Bello
Comment=Reference manager (Bello)
Exec=$DEST/bin/bello
Icon=$DEST/logo.svg
Type=Application
Categories=Office;Education;
Terminal=false
EOF

	echo "Installed to $DEST; desktop entry created at $DESKTOP_FILE"
	exit 0
fi

if [[ $NO_RUN -eq 1 ]]; then
	echo "--no-run provided; exiting without starting the app."
	exit 0
fi

BIN="$BUILD_DIR/bello"
if [[ ! -x "$BIN" ]]; then
	echo "Error: built binary not found or not executable: $BIN" >&2
	exit 1
fi

echo "Starting bello..."

# If we linked against the static library, ensure a copy exists in the build output
# (not required at runtime for static link, but makes the build tree self-contained)
if [[ "$DUCKDB_LIB_FILE" == *"_static.a" ]]; then
	mkdir -p "$BUILD_DIR/duckdb"
	cp -v "$DUCKDB_LIB_FILE" "$BUILD_DIR/duckdb/" || true
	exec "$BIN"
fi

# If we linked against a shared library, the CMake setup will copy the in-repo
# `duckdb/` directory into the build output (post-build). If that happened the
# rpath should resolve; run the binary directly.
exec "$BIN"
