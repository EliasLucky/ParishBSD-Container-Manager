#!/bin/sh
# libexec/parishbsd/lib/os_upgrade.sh
#
# FreeBSD OS version inspection, minor/major upgrade, ZFS snapshot safety,
# and recovery helpers for ParishBSD compartments and templates.

PARISH_UPGRADE_STATE_DIR="/var/db/parishbsd/upgrade-state"
PARISH_CACHE_DIR="/var/cache/parishbsd"

# parish_host_version_full -> e.g. "15.1-RELEASE-p2" (fallback "unknown")
parish_host_version_full() {
    /bin/freebsd-version -u 2>/dev/null | tr -d '[:space:]' || printf 'unknown'
}

parish_host_os_version() {
    parish_host_version_full
}

parish_host_os_arch() {
    /usr/bin/uname -m 2>/dev/null | tr -d '[:space:]' || printf 'amd64'
}

# parish_root_version_full <root> -> reads <root>/bin/freebsd-version safely
# without executing untrusted binaries inside the jail.
parish_root_version_full() {
    _prv_root="${1-}"
    _prv_fv="$_prv_root/bin/freebsd-version"
    if [ -f "$_prv_fv" ] && [ ! -L "$_prv_fv" ]; then
        _prv_v=$(awk -F= '/^USERLAND_VERSION=/{
            v=$2; gsub(/["'\''[:space:]]/, "", v); print v; exit
        }' "$_prv_fv" 2>/dev/null || true)
        if [ -n "$_prv_v" ]; then
            printf '%s' "$_prv_v"
            return 0
        fi
    fi
    if [ -x "$_prv_fv" ] && [ "$(id -u)" -eq 0 ]; then
        _prv_v=$(/usr/sbin/chroot "$_prv_root" /bin/freebsd-version -u 2>/dev/null | tr -d '[:space:]' || true)
        if [ -n "$_prv_v" ]; then
            printf '%s' "$_prv_v"
            return 0
        fi
    fi
    printf 'unknown'
}

parish_root_os_version() {
    parish_root_version_full "${1-}"
}

parish_root_os_arch() {
    _pra_root="${1-}"
    if [ -f "$_pra_root/etc/pkg/FreeBSD.conf" ]; then
        _pra_abi=$(awk '/url:.*FreeBSD:/{ if (match($0, /FreeBSD:[0-9]+:[a-z0-9_]+/)) { print substr($0, RSTART, RLENGTH); exit } }' "$_pra_root/etc/pkg/FreeBSD.conf" 2>/dev/null || true)
        case "$_pra_abi" in
            FreeBSD:*:*)
                printf '%s' "${_pra_abi##*:}"
                return 0
                ;;
        esac
    fi
    if [ -f "$_pra_root/bin/sh" ] && command -v file >/dev/null 2>&1; then
        _pra_f=$(file -b "$_pra_root/bin/sh" 2>/dev/null || true)
        case "$_pra_f" in
            *aarch64*) printf 'arm64'; return 0 ;;
            *x86-64*|*amd64*) printf 'amd64'; return 0 ;;
            *i386*|*80386*) printf 'i386'; return 0 ;;
        esac
    fi
    parish_host_os_arch
}

# parish_strip_patch <ver> -> "15.1-RELEASE-p2" -> "15.1-RELEASE"
parish_strip_patch() {
    printf '%s' "${1-}" | sed 's/-p[0-9][0-9]*$//'
}

parish_release_branch() {
    parish_strip_patch "${1-}"
}

# parish_version_major <ver> -> "15"
parish_version_major() {
    printf '%s' "${1-}" | awk -F. '{ if ($1 ~ /^[0-9]+$/) print $1 }'
}

# parish_version_minor <ver> -> "1"
parish_version_minor() {
    printf '%s' "${1-}" | sed 's/^[0-9]*\.//; s/[-_].*$//' | awk '{ if ($1 ~ /^[0-9]+$/) print $1 }'
}

# parish_version_patch <ver> -> integer patch level (0 if none)
parish_version_patch() {
    case "${1-}" in
        *-p[0-9]*)
            printf '%s' "${1##*-p}" | awk '{ if ($1 ~ /^[0-9]+$/) print $1; else print 0 }'
            ;;
        *)
            printf '0'
            ;;
    esac
}

# parish_os_status <target_full> <host_full>
# Prints: current | patch-behind | minor-behind | major-behind | ahead | unknown
parish_os_status() {
    _pos_t="${1-}"
    _pos_h="${2-}"
    [ -n "$_pos_t" ] && [ "$_pos_t" != "unknown" ] || { printf 'unknown'; return 0; }
    [ -n "$_pos_h" ] && [ "$_pos_h" != "unknown" ] || { printf 'unknown'; return 0; }

    _tmaj=$(parish_version_major "$_pos_t")
    _tmin=$(parish_version_minor "$_pos_t")
    _tpat=$(parish_version_patch "$_pos_t")
    _hmaj=$(parish_version_major "$_pos_h")
    _hmin=$(parish_version_minor "$_pos_h")
    _hpat=$(parish_version_patch "$_pos_h")

    if [ -z "$_tmaj" ] || [ -z "$_hmaj" ]; then
        if [ "$_pos_t" = "$_pos_h" ]; then
            printf 'current'
        else
            printf 'minor-behind'
        fi
        return 0
    fi
    [ -z "$_tmin" ] && _tmin=0
    [ -z "$_hmin" ] && _hmin=0

    if [ "$_tmaj" -lt "$_hmaj" ]; then
        printf 'major-behind'
    elif [ "$_tmaj" -gt "$_hmaj" ]; then
        printf 'ahead'
    elif [ "$_tmin" -lt "$_hmin" ]; then
        printf 'minor-behind'
    elif [ "$_tmin" -gt "$_hmin" ]; then
        printf 'ahead'
    elif [ "$_tpat" -lt "$_hpat" ]; then
        printf 'patch-behind'
    elif [ "$_tpat" -gt "$_hpat" ]; then
        printf 'ahead'
    else
        printf 'current'
    fi
}

parish_version_status() {
    parish_os_status "${1-}" "${2-}"
}

# parish_release_compare <a> <b> -> echoes -1 (a<b), 0 (a==b), 1 (a>b), or 99 (invalid)
parish_release_compare() {
    _amaj=$(parish_version_major "${1-}")
    _amin=$(parish_version_minor "${1-}")
    _bmaj=$(parish_version_major "${2-}")
    _bmin=$(parish_version_minor "${2-}")
    if [ -z "$_amaj" ] || [ -z "$_bmaj" ]; then
        printf '99'
        return 0
    fi
    [ -z "$_amin" ] && _amin=0
    [ -z "$_bmin" ] && _bmin=0
    if [ "$_amaj" -lt "$_bmaj" ]; then printf '%s' '-1'
    elif [ "$_amaj" -gt "$_bmaj" ]; then printf '1'
    elif [ "$_amin" -lt "$_bmin" ]; then printf '%s' '-1'
    elif [ "$_amin" -gt "$_bmin" ]; then printf '1'
    else printf '0'
    fi
}

# parish_validate_release_string <rel> -> e.g. 15.1-RELEASE
parish_validate_release_string() {
    case "${1-}" in
        [0-9]*.[0-9]*-RELEASE)
            _pvr_maj=$(parish_version_major "$1")
            _pvr_min=$(parish_version_minor "$1")
            [ -n "$_pvr_maj" ] && [ -n "$_pvr_min" ] && return 0
            ;;
    esac
    return 1
}

# parish_root_uses_pkgbase <root>
parish_root_uses_pkgbase() {
    _prup_root="${1-}"
    /usr/local/sbin/pkg -r "$_prup_root" info -e FreeBSD-runtime >/dev/null 2>&1 || \
    /usr/local/sbin/pkg -r "$_prup_root" info -e FreeBSD-clibs >/dev/null 2>&1
}

# parish_list_pre_upgrade_snapshots <dataset>
# Prints snapshot short names (newest first) matching pre-upgrade-*, pre-update-*, or base-*
parish_list_pre_upgrade_snapshots() {
    _plp_ds="${1-}"
    /sbin/zfs list -H -t snapshot -S creation -o name -d 1 "$_plp_ds" 2>/dev/null | \
        sed 's/^.*@//' | grep -E '^(pre-upgrade-|pre-update-|base-)' || true
}

# parish_list_snapshots <dataset> -> prints "<short_name>\t<creation>" (newest first)
parish_list_snapshots() {
    _pls_ds="${1-}"
    /sbin/zfs list -H -t snapshot -S creation -o name,creation -d 1 "$_pls_ds" 2>/dev/null | \
        awk -F'\t' '{ sub(/^.*@/, "", $1); if ($1 ~ /^(pre-upgrade-|pre-update-|base-)/) printf "%s\t%s\n", $1, $2 }' || true
}

parish_latest_recovery_snapshot() {
    _plr_ds="${1-}"
    /sbin/zfs list -H -t snapshot -S creation -o name -d 1 "$_plr_ds" 2>/dev/null | \
        sed 's/^.*@//' | grep -E '^(pre-upgrade-|pre-update-|base-)' | head -n 1 || true
}

parish_upgrade_state_file() {
    if [ "${2+set}" = "set" ]; then
        printf '%s/%s-%s.conf' "$PARISH_UPGRADE_STATE_DIR" "$1" "$2"
    else
        printf '%s/%s.state' "$PARISH_UPGRADE_STATE_DIR" "$1"
    fi
}

parish_has_interrupted_upgrade() {
    _phi_kind="${1-compartment}"
    _phi_name="${2-}"
    if [ -z "$_phi_name" ]; then
        _phi_name="$_phi_kind"
        _phi_kind="compartment"
    fi
    if [ "$_phi_kind" = "template" ]; then
        _phi_tc="$PARISH_UPGRADE_STATE_DIR/template-$_phi_name.conf"
        [ -f "$_phi_tc" ] && [ ! -L "$_phi_tc" ] && return 0
        return 1
    fi
    _phi_st="$PARISH_UPGRADE_STATE_DIR/$_phi_name.state"
    if [ -f "$_phi_st" ] && [ ! -L "$_phi_st" ]; then
        _phi_val=$(awk -F= '$1=="STATE"{print $2; exit}' "$_phi_st" 2>/dev/null || true)
        [ "$_phi_val" = "in-progress" ] && return 0
    fi
    _phi_legacy="$PARISH_UPGRADE_STATE_DIR/compartment-$_phi_name.conf"
    [ -f "$_phi_legacy" ] && [ ! -L "$_phi_legacy" ]
}

parish_upgrade_interrupted() {
    if [ "$#" -ge 2 ]; then
        parish_has_interrupted_upgrade "$1" "$2"
    else
        parish_has_interrupted_upgrade "compartment" "${1-}"
    fi
}

# parish_write_jail_fbsd_update_conf <out_path> <workdir>
# Creates a jail-safe freebsd-update.conf (world-only, no kernel updates,
# preserves ParishBSD metadata, user homes, and vaults).
parish_write_jail_fbsd_update_conf() {
    _pw_conf="$1"
    _pw_work="$2"
    cat > "$_pw_conf" <<EOF
KeyPrint 800651ef4b4c71c27e60786d7b487188970f4b4169cc055784e21eb71d410cc5
ServerName update.FreeBSD.org
Components world
IgnorePaths /usr/local/etc/parishbsd /var/db/parishbsd /home /vault /exchange /compat
IDSIgnorePaths /usr/local/etc/parishbsd /var/db/parishbsd /home /vault /exchange /compat
UpdateIfUnmodified /etc/ /var/ /root/
MergeChanges /etc/ /var/named/etc/
WorkDir ${_pw_work}
MailTo root
AllowAdd yes
AllowDelete yes
KeepModifiedMetadata yes
StrictComponents no
BackupKernel no
CreateBootEnv no
EOF
}

# parish_backup_jail_configs <root> <backup_dir>
parish_backup_jail_configs() {
    _pb_root="$1"
    _pb_dst="$2"
    mkdir -p "$_pb_dst"
    for _rel in \
        etc/resolv.conf \
        etc/hosts \
        etc/rc.conf \
        etc/fstab \
        etc/master.passwd \
        etc/group \
        usr/local/etc/parishbsd/metadata \
        usr/local/etc/pkg/repos/ParishBSD.conf
    do
        if [ -f "$_pb_root/$_rel" ] && [ ! -L "$_pb_root/$_rel" ]; then
            mkdir -p "$_pb_dst/$(dirname "$_rel")"
            cp -p "$_pb_root/$_rel" "$_pb_dst/$_rel" 2>/dev/null || true
        fi
    done
}

# parish_restore_jail_configs <root> <backup_dir>
parish_restore_jail_configs() {
    _pr_root="$1"
    _pr_src="$2"
    for _rel in \
        etc/hosts \
        etc/rc.conf \
        etc/fstab \
        usr/local/etc/parishbsd/metadata \
        usr/local/etc/pkg/repos/ParishBSD.conf
    do
        if [ -f "$_pr_src/$_rel" ]; then
            mkdir -p "$_pr_root/$(dirname "$_rel")"
            cp -p "$_pr_src/$_rel" "$_pr_root/$_rel" 2>/dev/null || true
        fi
    done
    # Only restore resolv.conf if backup existed (non-vaults)
    if [ -f "$_pr_src/etc/resolv.conf" ] && [ -d "$_pr_root/etc" ]; then
        cp -p "$_pr_src/etc/resolv.conf" "$_pr_root/etc/resolv.conf" 2>/dev/null || true
    fi
    if [ -f "$_pr_root/etc/master.passwd" ]; then
        /usr/sbin/pwd_mkdb -d "$_pr_root/etc" -p "$_pr_root/etc/master.passwd" >/dev/null 2>&1 || true
    fi
}

# parish_prune_old_pre_upgrade_snaps <dataset> [keep_count]
parish_prune_old_pre_upgrade_snaps() {
    _pp_ds="$1"
    _pp_keep="${2:-3}"
    _pp_idx=0
    /sbin/zfs list -H -t snapshot -S creation -o name -d 1 "$_pp_ds" 2>/dev/null | \
        grep '@pre-upgrade-' | while IFS= read -r _snap; do
        _pp_idx=$((_pp_idx + 1))
        if [ "$_pp_idx" -gt "$_pp_keep" ]; then
            _clones=$(/sbin/zfs get -H -o value clones "$_snap" 2>/dev/null || echo "-")
            if [ "$_clones" = "-" ]; then
                /sbin/zfs destroy "$_snap" >/dev/null 2>&1 || true
            fi
        fi
    done
}
