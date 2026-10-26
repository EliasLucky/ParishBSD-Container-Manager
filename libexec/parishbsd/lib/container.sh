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
container_personality() {
    _jailroot="${1-}"

    # Tier class.
    case "$_jailroot" in
        /usr/local/jails/app/*)        _tier_class=app ;;
        /usr/local/jails/containers/*) _tier_class=app ;;
        /usr/local/jails/vault/*)      _tier_class=vault ;;
        *) printf 'unknown'; return ;;
    esac

    # Metadata is advisory (may narrow within tier; never cross).
    _meta="$_jailroot/usr/local/etc/parishbsd/metadata"
    _personality=""
    if [ -f "$_meta" ] && [ ! -L "$_meta" ]; then
        while IFS='=' read -r _key _val; do
            case "$_key" in
                PERSONALITY) _personality="$_val" ;;
            esac
        done < "$_meta"

        if [ -n "$_personality" ]; then
            validate_personality_name "$_personality" 2>/dev/null \
                || _personality=""
        fi
    fi

    # Cross-check. Metadata is trusted only if consistent with tier.
    # Any mismatch  fall back to tier-derived personality.
    case "$_tier_class:$_personality" in
        app:app-*|app:)
            printf '%s' "${_personality:-app-xpra}"
            ;;
        vault:vault-*)
            printf '%s' "$_personality"
            ;;
        vault:)
            # Empty/invalid metadata; infer sub-kind from marker dirs
            # (also host-side, cannot be faked).
            if [ -d "$_jailroot/var/vault-data" ]; then
                printf 'vault-files'
            elif [ -d "$_jailroot/var/db/postgres" ]; then
                printf 'vault-postgres'
            else
                printf 'vault-unknown'
            fi
            ;;
        *)
            # Tier/metadata mismatch. The jail is lying or metadata
            # is stale. Trust the tier.
            case "$_tier_class" in
                app)   printf 'app-xpra' ;;
                vault) printf 'vault-unknown' ;;
            esac
            ;;
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