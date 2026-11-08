#!/bin/sh
# 25-setup-templates.sh  bootstrap ZFS layout, base cache, shipped templates.
#
# Idempotent. Does not rebuild existing templates (build refuses if dataset
# exists). Set PARISHBSD_FORCE=1 to re-download base.txz; template rebuild
# requires explicit `parishctl template reset <name>`.
set -eu

PARISHBSD_CACHE="${PARISHBSD_CACHE:-/var/cache/parishbsd}"
PARISHBSD_FORCE="${PARISHBSD_FORCE:-0}"

log() { printf '    %s\n' "$*"; }
err() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || err "must be run as root"
command -v zfs >/dev/null 2>&1 || err "zfs not found"
command -v fetch >/dev/null 2>&1 || command -v curl >/dev/null 2>&1 \
    || err "fetch or curl not found"

log "Ensuring ZFS layout"
zfs list zroot/jails            >/dev/null 2>&1 || zfs create -p zroot/jails
zfs list zroot/jails/templates  >/dev/null 2>&1 || zfs create -p zroot/jails/templates
zfs list zroot/jails/containers >/dev/null 2>&1 || zfs create -p zroot/jails/containers
zfs list zroot/vault-data       >/dev/null 2>&1 || zfs create -p zroot/vault-data

_ensure_ds() {
    _ds="$1"; _mp="$2"
    zfs list "$_ds" >/dev/null 2>&1 || zfs create -p "$_ds" || return 1
    [ "$(zfs get -H -o value mountpoint "$_ds")" = "$_mp" ] || \
        zfs set mountpoint="$_mp" "$_ds" || return 1
    [ "$(zfs get -H -o value mounted "$_ds")" = "yes" ] || \
        zfs mount "$_ds" 2>/dev/null || return 1
}

_ensure_ds zroot/jails            /zroot/jails
_ensure_ds zroot/jails/templates  /zroot/jails/templates
_ensure_ds zroot/jails/containers /zroot/jails/containers
_ensure_ds zroot/vault-data       /zroot/vault-data

arch="$(uname -m)"
version="$(freebsd-version -u | awk -F'-p' '{print $1}')"
cached_txz="$PARISHBSD_CACHE/base-$version-$arch.txz"
url="https://download.freebsd.org/releases/$arch/$version/base.txz"

log "Ensuring base.txz cache"
mkdir -p "$PARISHBSD_CACHE"
if [ -f "$cached_txz" ] && [ "$PARISHBSD_FORCE" != "1" ]; then
    log "  using cached $cached_txz"
else
    log "  downloading $url"
    tmp="$cached_txz.part"
    rm -f "$tmp"
    if command -v fetch >/dev/null 2>&1; then
        fetch -o "$tmp" "$url" || err "download failed"
    else
        curl -L -o "$tmp" "$url" || err "download failed"
    fi
    mv "$tmp" "$cached_txz"
    log "  cached ($(du -h "$cached_txz" | awk '{print $1}'))"
fi

log "Building shipped templates"
for tmpl in apps apps-compatible vault-postgres vault-files; do
    if zfs list "zroot/jails/templates/$tmpl" >/dev/null 2>&1; then
        log "  $tmpl: already built"
        continue
    fi
    log "  $tmpl: building..."
    parish-template-build "$tmpl" --source=shipped \
        || err "parish-template-build $tmpl failed"
done

log "All shipped templates built."
log "Verify with: zfs list -t snapshot -r zroot/jails/templates"
