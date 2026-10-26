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
# container_kind_to_personality <kind>
# Map user-facing kind aliases to personality names. Inverse of
# container_kind for alias inputs; identity for personality names
# already in canonical form.
container_kind_to_personality() {
    case "${1-}" in
        app|application)                           printf 'app-xpra' ;;
        vault-files|file-vault|files|files-vault)  printf 'vault-files' ;;
        vault-postgres|vault-pg|database|db)       printf 'vault-postgres' ;;
        *)                                         printf '%s' "${1-}" ;;
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
# container_start <name>
# Start a compartment. Handles vault unlock, TTY refusals, and
# post-condition verification. Prints user-facing messages.
#
# Returns 0 on success (jail verified running), 1 on any failure.
# Callers should propagate the exit code.
container_start() {
    _name="${1-}"
    [ -n "$_name" ] || return 1

    _jailroot="$(container_path "$_name")" || {
        printf 'parishctl: no compartment named "%s"\n' "$_name" >&2
        return 1
    }

    _personality="$(container_personality "$_jailroot")"
    _state="$(parish_jail_state "$_name")"

    case "$_state" in
        running|running-other-contained)
            printf 'Container "%s" is already running.\n' "$_name"
            return 0
            ;;
        stopped)
            : # proceed
            ;;
        *)
            parish_jail_state_message "$_name" "$_state" >&2
            return 1
            ;;
    esac

    if container_is_vault "$_personality"; then
        _ds="zroot/vault-data/$_name"
        _keystatus="$(zfs get -H -o value keystatus "$_ds" 2>/dev/null)"
        case "$_keystatus" in
            available) : ;;
            unavailable)
                if [ ! -t 0 ]; then
                    cat >&2 <<EOF
The vault "$_name" is locked.

A vault needs a passphrase to unlock. Starting it from the desktop
does not give you a place to type the passphrase.

To unlock it:
  1. Open a terminal (in the applications menu, under System or Utilities).
  2. Run: pkexec parishctl unlock $_name
  3. Type the passphrase when prompted.
  4. Start the compartment again.
EOF
                    return 1
                fi
                ;;
            *)
                cat >&2 <<EOF
Cannot determine the state of vault "$_name".
The ZFS dataset is "$_ds" and "zfs get keystatus" did not return a
usable value (got '${_keystatus:-<empty>}').

This usually means one of:
  - The dataset does not exist (was the vault destroyed?).
  - The ZFS pool is not imported (try: zpool import).
  - The dataset is not encrypted (was it created correctly?).
  - ZFS is not working correctly on this system.

Check the state with:
  zfs get keystatus $_ds
  zpool status
EOF
                return 1
                ;;
        esac

        sh /usr/local/libexec/parishbsd/27-unlock-vault.sh "$_name" \
            || { printf 'parishctl: failed to unlock and start vault "%s"\n' "$_name" >&2; return 1; }
    else
        service jail start "$_name" \
            || { printf 'parishctl: failed to start "%s"\n' "$_name" >&2; return 1; }
    fi

    # Post-condition (P5/A8): start reported success; verify state.
    _state_after="$(parish_jail_state "$_name")"
    case "$_state_after" in
        running|running-other-contained)
            return 0
            ;;
        containing|contained|contained-but-running|containment-failed|stopped-maybe-contained)
            parish_jail_state_message "$_name" "$_state_after" >&2
            return 1
            ;;
        *)
            printf 'parishctl: start command returned success but the container is "%s".\n' \
                "$_state_after" >&2
            printf 'This is unexpected. Check:\n' >&2
            printf '  service jail status %s\n' "$_name" >&2
            printf '  tail /var/log/messages\n' >&2
            return 1
            ;;
    esac
}

# container_stop <name>
# Stop a compartment. Prints user-facing messages. Returns 0 on
# success (jail verified stopped), 1 on any failure.
container_stop() {
    _name="${1-}"
    [ -n "$_name" ] || return 1

    _jailroot="$(container_path "$_name")" || {
        printf 'parishctl: no compartment named "%s"\n' "$_name" >&2
        return 1
    }

    _personality="$(container_personality "$_jailroot")"
    _state="$(parish_jail_state "$_name")"

    case "$_state" in
        stopped)
            printf 'Container "%s" is not running.\n' "$_name"
            return 0
            ;;
        containing|contained|containment-failed|contained-but-running|stopped-maybe-contained|unknown)
            parish_jail_state_message "$_name" "$_state" >&2
            return 1
            ;;
    esac

    service jail stop "$_name" \
        || { printf 'parishctl: failed to stop "%s"\n' "$_name" >&2; return 1; }

    printf 'Container "%s" stopped.\n' "$_name"

    if container_is_vault "$_personality"; then
        printf '\n'
        printf 'Note: the vault is still unlocked. To lock it:\n'
        printf '    pkexec parishctl lock %s\n' "$_name"
    fi

    return 0
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