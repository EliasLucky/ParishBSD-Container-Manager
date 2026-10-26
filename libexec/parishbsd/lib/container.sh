#!/bin/sh
# libexec/parishbsd/lib/container.sh
#
# Container state-inspection primitives. Reads the host's view of a
# jail's configuration: root path, personality, kind, vault lock state.
#
# REQUIRES (sourced by the caller, in this order):
#   validate.sh     jail name and personality name validators
#   container.sh    this file

# container_path <name>
# Echo the jail root path for a container, or return 1 if not found.
container_path() {
    _name="${1-}"
    [ -n "$_name" ] || return 1
    for _tier in app vault containers; do
        _d="/usr/local/jails/$_tier/$_name"
        if [ -d "$_d" ]; then
            printf '%s' "$_d"
            return 0
        fi
    done
    return 1
}

# container_personality <jailroot>
# Echo the personality name. Safe against jail-written metadata.
#
# Do NOT source the metadata file. Do NOT trust its
# value without validation  a jail can write PERSONALITY=../../etc.
#
# The ownership check: a jail running as root
# inside itself can only create files with the host-uid that maps to
# its own root - for a jailed process that is a *different* host user
# from root (the jail's uid 0 is not host uid 0). So stat -f %Su
# returning "root" is a real host-side signal that the file was
# created by host-root (at jail creation).
#
# However, if the jail is running as a *host* root user via jexec
# (which is the default jexec behavior), files created inside can
# legitimately be host-uid-0. The ownership check therefore does not
# fully prevent a jail that has jexec-equivalent access from lying
# about its personality. The remaining defense is the tier fallback:
# if PERSONALITY is invalid or the file fails checks, we infer from
# the directory (/usr/local/jails/app/ vs /usr/local/jails/vault/),
# which the jail cannot lie about because it is host filesystem
# structure outside the jail's writable area.
container_personality() {
    _jailroot="${1-}"
    _meta="$_jailroot/usr/local/etc/parishbsd/metadata"

    if [ -f "$_meta" ] && [ ! -L "$_meta" ] \
       && [ "$(stat -f %Su "$_meta" 2>/dev/null)" = "root" ] \
       && [ "$(stat -f %Lp "$_meta" 2>/dev/null)" = "644" ]; then
        _personality=""
        while IFS='=' read -r _key _val; do
            case "$_key" in
                PERSONALITY) _personality="$_val" ;;
            esac
        done < "$_meta"

        if [ -n "$_personality" ]; then
            validate_personality_name "$_personality" 2>/dev/null \
                || _personality=""
        fi
        if [ -n "$_personality" ]; then
            printf '%s' "$_personality"
            return
        fi
    fi

    # Tier fallback. Cannot be influenced by the jail.
    case "$_jailroot" in
        /usr/local/jails/app/*)        printf 'app-xpra' ;;
        /usr/local/jails/containers/*) printf 'app-xpra' ;;
        /usr/local/jails/vault/*)
            if [ -d "$_jailroot/var/vault-data" ]; then
                printf 'vault-files'
            elif [ -d "$_jailroot/var/db/postgres" ]; then
                printf 'vault-postgres'
            else
                printf 'vault-unknown'
            fi
            ;;
        *) printf 'unknown' ;;
    esac
}

# container_kind <personality>
# User-facing label ("Application", "File Vault", "Database Vault").
container_kind() {
    case "${1-}" in
        app-xpra)       printf 'Application' ;;
        vault-files)    printf 'File Vault' ;;
        vault-postgres) printf 'Database Vault' ;;
        *)              printf '%s' "${1-}" ;;
    esac
}

# container_is_vault <personality>
container_is_vault() {
    case "${1-}" in vault-*) return 0 ;; *) return 1 ;; esac
}

# container_vault_lock_state <name>
# Echo locked | unlocked | (empty if the dataset does not exist).
container_vault_lock_state() {
    _name="${1-}"
    _ds="zroot/vault-data/$_name"
    zfs list "$_ds" >/dev/null 2>&1 || return 0
    _ks="$(zfs get -H -o value keystatus "$_ds" 2>/dev/null || printf unknown)"
    case "$_ks" in
        available) printf 'unlocked' ;;
        *)         printf 'locked' ;;
    esac
}

# container_list_names
# All container names, sorted. Scans app, vault, containers tiers.
container_list_names() {
    for _tier in app vault containers; do
        for _d in "/usr/local/jails/$_tier"/*; do
            [ -d "$_d" ] || continue
            basename "$_d"
        done
    done | sort -u
}