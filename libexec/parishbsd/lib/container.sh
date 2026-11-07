#!/bin/sh
# libexec/parishbsd/lib/container.sh
#
# Container state-inspection primitives. Reads the host's view of a
# jail's metadata: root path, source template, tier, kind, vault
# lock state.
#
# REQUIRES (sourced by the caller, in this order):
#   validate.sh     jail name and template name validators
#   state.sh        parish_jail_state, parish_jail_state_message
#   service.sh      parish_service_start, parish_service_stop
#   container.sh    this file
#
# LEGACY JAILS
# ------------
# A jail created has PERSONALITY= in its metadata and no
# SOURCE_TEMPLATE=. container_template() returns empty and
# container_tier() returns "legacy". Every caller that would change
# state (start, lock, unlock) MUST refuse legacy jails with a message
# naming parishctl-destroy as the recovery. container_stop() is exempt:
# stopping a running jail is valid regardless of how it was created.
#
# TIER
# ----
# tier= in template.conf is "app" or "vault". 30-create-jail.sh copies
# it to the jail's metadata as TIER=. container_is_vault() takes the
# tier value (not the jail name, not a personality name); prefix-based
# derivation died in 3b because Option A mountpoints are all under
# /usr/local/jails/containers/.

# container_path <name>
# Echo the jail root path for a container, or return 1 if not found.
container_path() {
    _name="${1-}"
    [ -n "$_name" ] || return 1
    for _tier_dir in app vault containers; do
        _d="/usr/local/jails/$_tier_dir/$_name"
        if [ -d "$_d" ]; then
            printf '%s' "$_d"
            return 0
        fi
    done
    return 1
}

# container_meta_field <jailroot> <field>
# Echo the field value from <jailroot>/usr/local/etc/parishbsd/metadata.
# Empty + return 1 if the file is absent, is a symlink, or lacks the field.
container_meta_field() {
    _jailroot="${1-}"
    _field="${2-}"
    [ -n "$_jailroot" ] && [ -n "$_field" ] || return 1
    _meta="$_jailroot/usr/local/etc/parishbsd/metadata"
    [ -f "$_meta" ] && [ ! -L "$_meta" ] || return 1
    while IFS='=' read -r _k _v; do
        if [ "$_k" = "$_field" ]; then
            printf '%s' "$_v"
            return 0
        fi
    done < "$_meta"
    return 1
}

# container_template <jailroot>
# Echo the source template name, or empty + return 1 for legacy jails.
container_template() {
    _tmpl="$(container_meta_field "${1-}" SOURCE_TEMPLATE)" || return 1
    [ -n "$_tmpl" ] || return 1
    validate_template_name "$_tmpl" 2>/dev/null || return 1
    printf '%s' "$_tmpl"
}

# container_tier <jailroot>
# Echo app|vault|legacy.
container_tier() {
    _t="$(container_meta_field "${1-}" TIER)" || { printf 'legacy'; return; }
    case "$_t" in
        app|vault) printf '%s' "$_t" ;;
        *)         printf 'legacy' ;;
    esac
}

# container_kind_label <jailroot>
# User-facing label ("Application", "File Vault", "Database Vault").
container_kind_label() {
    _k="$(container_meta_field "${1-}" KIND)" || _k=""
    case "$_k" in
        application)    printf 'Application' ;;
        file-vault)     printf 'File Vault' ;;
        database-vault) printf 'Database Vault' ;;
        *)
            case "$(container_tier "${1-}")" in
                vault) printf 'Vault' ;;
                *)     printf 'Application' ;;
            esac
            ;;
    esac
}

# container_is_vault <tier>
# <tier> is "app" | "vault" | "legacy".
container_is_vault() {
    [ "${1-}" = "vault" ]
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

# container_refuse_legacy <name> <jailroot>
# Print a consistent refusal for a legacy jail and return 1.
# Used by every state-changing caller (start, lock, unlock).
container_refuse_legacy() {
    printf 'parishctl: container "%s" was created before the template system and cannot be used.\n' "$1" >&2
    printf 'It must be destroyed and recreated:\n' >&2
    printf '  pkexec parishctl destroy %s\n' "$1" >&2
    return 1
}

# container_start <name>
# Start a compartment. Handles vault unlock, TTY refusals, legacy refusal,
# and post-condition verification. Prints user-facing messages.
container_start() {
    _name="${1-}"
    [ -n "$_name" ] || return 1

    _jailroot="$(container_path "$_name")" || {
        printf 'parishctl: no compartment named "%s"\n' "$_name" >&2
        return 1
    }

    _tier="$(container_tier "$_jailroot")"
    [ "$_tier" = "legacy" ] && { container_refuse_legacy "$_name" "$_jailroot"; return 1; }

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

    if container_is_vault "$_tier"; then
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
        parish_service_start "$_name" "$_jailroot" || return 1
    fi

    _state_after="$(parish_jail_state "$_name")"
    case "$_state_after" in
        running|running-other-contained)
            printf 'Container "%s" started.\n' "$_name"
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
# Stop a compartment. Works on any running jail regardless of tier;
# legacy jails can be stopped (they cannot be started/locked/unlocked).
container_stop() {
    _name="${1-}"
    [ -n "$_name" ] || return 1

    _jailroot="$(container_path "$_name")" || {
        printf 'parishctl: no compartment named "%s"\n' "$_name" >&2
        return 1
    }

    _tier="$(container_tier "$_jailroot")"
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

    parish_service_stop "$_name" || return 1

    printf 'Container "%s" stopped.\n' "$_name"

    if container_is_vault "$_tier"; then
        printf '\n'
        printf 'Note: the vault is still unlocked. To lock it:\n'
        printf '    pkexec parishctl lock %s\n' "$_name"
    fi

    return 0
}

# container_list_names
# All container names, sorted. Scans app, vault, containers tiers.
container_list_names() {
    for _tier_dir in app vault containers; do
        for _d in "/usr/local/jails/$_tier_dir"/*; do
            [ -d "$_d" ] || continue
            basename "$_d"
        done
    done | sort -u
}
