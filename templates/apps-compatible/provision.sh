#!/bin/sh
# apps-compatible/provision.sh  runs inside the jail on every start.
# Initializes the wine prefix on first start (sentinels on drive_c).
set -eu

log() { printf '    [apps-compatible] %s\n' "$*"; }

# wine prefix lives under the app user's home if run as app, or root
# if run as root. Currently apps run as root in-jail (G-runtime is a
# separate module). Use root's home.
PREFIX="/root/.wine64"

if [ -d "$PREFIX/drive_c" ]; then
    log "wine prefix already initialized"
    exit 0
fi

log "initializing wine prefix (first start, ~15s)"
mkdir -p "$(dirname "$PREFIX")"

if ! WINEARCH=win64 WINEPREFIX="$PREFIX" /usr/local/bin/wine64.bin wineboot --init >/dev/null 2>&1; then
    log "WARN: wineboot failed; wine prefix not initialized"
    log "WARN: first wine app launch will retry"
    exit 0    # non-fatal; wine retries on next invocation
fi

# wineboot is async; poll for drive_c
_i=0
while [ "$_i" -lt 60 ]; do
    [ -d "$PREFIX/drive_c" ] && { log "wine prefix ready"; exit 0; }
    sleep 0.5
    _i=$((_i + 1))
done

log "WARN: wine prefix not ready after 30s"
exit 0    # non-fatal
