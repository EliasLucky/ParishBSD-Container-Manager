#!/bin/sh
# libexec/parishbsd/lib/state.sh
#
# Jail state detection for ParishBSD Compartments.
#
# REQUIRES: /usr/local/libexec/parishbsd/lib/validate.sh must be sourced
# by the caller BEFORE this file, because parish_jail_state() calls
# validate_jail_name_existing() to guard against malformed input. Callers
# should do:
#
#     . /usr/local/libexec/parishbsd/lib/validate.sh
#     . /usr/local/libexec/parishbsd/lib/state.sh
#
# This is the "library depends on library" pattern.
# For defense in depth, parish_jail_state also validates internally and
# returns "unknown" on invalid input, so a caller that forgot to source
# validate.sh is degraded, not exploited.
#
# Reports one of ten states. Containment records are checked before kernel
# truth (jls), because "jls says running" does not mean "safe": a jail
# Guardian contained and something restarted is more dangerous than a jail
# the user has been running all along.
#
# States (priority order):
#
#   containing              incident.*.conf mentions this jail
#   (released+running)      released/<jail> exists, jls running → running
#   (released+stopped)      released/<jail> exists, jls stopped → stopped
#   containment-failed      detection.conf describes this jail, level=containment-failed
#   contained-but-running   detection.conf describes this jail, level=contained, jls running
#   contained               detection.conf describes this jail, level=contained
#   running-other-contained detection.conf describes another jail, jls running
#   stopped-maybe-contained detection.conf describes another jail, jls stopped
#   running                 no containment record, jls running
#   stopped                 no containment record, jls stopped
#   unknown                 cannot determine (invalid jail name; missing dirs)
#
# Note: `stopped` is returned from both the released-marker path (layer 2)
# and the no-containment path (layer 5). Callers cannot distinguish. This is
# intentional: for every current caller, a released jail that is stopped is
# a normal stopped jail. If a future caller needs a "recently released"
# banner, add `released` as a distinct state and update callers; do not
# silently change `stopped`'s meaning.
#
# /var/run is tmpfs on FreeBSD. A file under /var/run is from this boot by
# construction. If this assumption ever breaks, add a birth-time check:
#     stat -f %B /var/run/parishbsd/detection.conf
#     sysctl -n kern.boottime
# and treat files older than boot as absent. Do NOT add this check
# speculatively; it costs two subprocesses per state check.

parish_jail_state() {
    _jail="${1:?parish_jail_state: jail name required}"

    # Defensive validation. Even if callers were supposed to validate, the library must not be exploitable if a caller forgot. A newline
    # in _jail would make grep -Fqx treat it as multiple patterns; a leading "-" would make jls -j interpret it as a flag.
    if ! validate_jail_name_existing "$_jail" 2>/dev/null; then
        printf 'unknown\n'
        return 0
    fi

    _rundir="/var/run/parishbsd"
    _detection="$_rundir/detection.conf"

    # --- Layer 1: containment in flight ---
    for _f in "$_rundir"/incident.*.conf; do
        [ -e "$_f" ] || continue
        [ ! -L "$_f" ] || continue
        if grep -Fqx "jail=${_jail}" "$_f" 2>/dev/null; then
            printf 'containing\n'
            return 0
        fi
    done

    # --- Layer 2: released marker ---
    # A released jail is not contained. It may still be running (if an admin started it directly) or stopped. Report the truth about
    # running; the marker is a statement about containment, not about running.
    if [ -f "$_rundir/released/$_jail" ] &&
       [ ! -L "$_rundir/released/$_jail" ]; then
        if _parish_jls_running "$_jail"; then
            printf 'running\n'
        else
            printf 'stopped\n'
        fi
        return 0
    fi

    # --- Layer 3: detection.conf describes THIS jail ---
    if [ -f "$_detection" ] && [ ! -L "$_detection" ] &&
       grep -Fqx "jail=${_jail}" "$_detection" 2>/dev/null; then

        if grep -Fqx 'level=containment-failed' "$_detection" 2>/dev/null; then
            printf 'containment-failed\n'
            return 0
        fi

        if _parish_jls_running "$_jail"; then
            printf 'contained-but-running\n'
            return 0
        fi

        printf 'contained\n'
        return 0
    fi

    # --- Layer 4: detection.conf describes ANOTHER jail ---
    if [ -f "$_detection" ] && [ ! -L "$_detection" ]; then
        if _parish_jls_running "$_jail"; then
            printf 'running-other-contained\n'
            return 0
        fi
        if _parish_jail_dir_exists "$_jail"; then
            printf 'stopped-maybe-contained\n'
            return 0
        fi
        printf 'unknown\n'
        return 0
    fi

    # --- Layer 5: no containment record ---
    if _parish_jls_running "$_jail"; then
        printf 'running\n'
        return 0
    fi
    if _parish_jail_dir_exists "$_jail"; then
        printf 'stopped\n'
        return 0
    fi

    printf 'unknown\n'
}

_parish_jls_running() {
    _jid="$(jls -j "$1" jid 2>/dev/null)" || return 1
    case "$_jid" in
        ''|*[!0-9]*) return 1 ;;
    esac
    return 0
}

_parish_jail_dir_exists() {
    [ -d "/usr/local/jails/app/$1" ] ||
    [ -d "/usr/local/jails/vault/$1" ] ||
    [ -d "/usr/local/jails/containers/$1" ]
}

# Feature probe: does the installed Guardian support `release`?
# Lazy-initialized and cached so the probe runs at most once per shell.
# Called only from message functions, not from parish_jail_state (so
# the state function does not invoke a subprocess just to produce a
# state string).
_parish_has_release() {
    if [ -z "${_PARISH_HAS_RELEASE+x}" ]; then
        if command -v parish-guardian-admin >/dev/null 2>&1 && \
           parish-guardian-admin --help 2>/dev/null | grep -qF 'release'; then
            _PARISH_HAS_RELEASE=1
        else
            _PARISH_HAS_RELEASE=0
        fi
    fi
    [ "$_PARISH_HAS_RELEASE" = "1" ]
}

parish_jail_state_message() {
    _jail="${1:?parish_jail_state_message: jail name required}"
    _state="${2:?parish_jail_state_message: state required}"

    case "$_state" in
        containing)
            printf 'This compartment is being contained by the Guardian right now. Wait for the alert to appear (usually within a few seconds), then follow the instructions on the screen.\n\nIf no alert appears after a minute, restart the computer.\n'
            ;;
        contained)
            printf 'The compartment "%s" has been contained by the Guardian because suspicious activity was detected.\n\nDo not start it. Review the incident first.\n\nTo review the incident: open the Compartments Manager and select "Show incident".\n' "$_jail"
            if _parish_has_release; then
                printf '\nIf you are certain this was a false positive, an administrator can release the containment with:\n    parish-guardian-admin release %s --reason=<text>\n' "$_jail"
            else
                printf '\nTo clear the containment, restart the computer.\n'
            fi
            ;;
        containment-failed)
            printf 'The Guardian tried to contain the compartment "%s" and failed. The compartment may still be running and may be compromised.\n\nDo not use the compartment, do not copy files out of it, and do not trust anything inside it.\n\nRestart the computer into the previous boot environment. If that is not possible, see /var/log/parishbsd/incidents.log for details.\n' "$_jail"
            ;;
        contained-but-running)
            printf 'The compartment "%s" was contained by the Guardian and is running again despite this.\n\nThis should not be possible. Do not use the compartment, do not copy files out of it, and do not trust anything inside it.\n\nRestart the computer into the previous boot environment. If that is not possible, see /var/log/parishbsd/incidents.log for details.\n' "$_jail"
            ;;
        stopped-maybe-contained)
            printf 'Another compartment was recently contained by the Guardian. Because the computer has not been restarted since, the history of "%s" cannot be confirmed safe.\n\nTo start it, restart the computer first.\n' "$_jail"
            if _parish_has_release; then
                printf '\nIf you are certain this was a false positive, an administrator can release the containment of the affected compartment with:\n    parish-guardian-admin release <name> --reason=<text>\n'
            fi
            ;;
        running-other-contained)
            printf 'The compartment "%s" is running. Another compartment was recently contained by the Guardian.\n\nYou may continue using "%s". To check the current containment status, open the Compartments Manager.\n' "$_jail" "$_jail"
            ;;
        running)
            printf 'The compartment "%s" is running.\n' "$_jail"
            ;;
        stopped)
            printf 'The compartment "%s" is stopped.\n' "$_jail"
            ;;
        *)
            printf 'The state of compartment "%s" could not be determined safely. For your protection, the action was refused.\n\nIf this continues, run: parish-guardian-admin show %s\n' "$_jail" "$_jail"
            ;;
    esac
}
# parish_require_not_contained <jail>
#
# Return 0 if the compartment is in a state where state-changing
# operations are safe. Return 1 (with message on stderr) if the
# compartment is in a containment state or an unclear state.
#
# Callers:
#   parish_require_not_contained "$jail" || exit 1
#
# Allowed: running, running-other-contained, stopped
#   - running and running-other-contained are the normal operation
#     states.
#   - stopped is allowed because many state-changing operations work
#     on stopped jails (parish-exchange filesystem transfers,
#     parish-app-create-launcher metadata writes, parish-apps
#     remove-custom).
#
# Refused: containing, contained, containment-failed,
# contained-but-running, stopped-maybe-contained, unknown
#   - containing: containment in flight
#   - contained: successfully contained
#   - containment-failed: emergency (jail may still be running)
#   - contained-but-running: invariant violation ("should not happen")
#   - stopped-maybe-contained: another jail was contained this boot;
#     this jail's history is not confirmed safe
#   - unknown: state cannot be determined
parish_require_not_contained() {
    _rnc_jail="${1-}"
    if [ -z "$_rnc_jail" ]; then
        printf 'parish: jail name required\n' >&2
        return 1
    fi
    validate_jail_name_existing "$_rnc_jail" || return 1
    _rnc_state="$(parish_jail_state "$_rnc_jail")"
    case "$_rnc_state" in
        running|running-other-contained|stopped)
            return 0
            ;;
        *)
            parish_jail_state_message "$_rnc_jail" "$_rnc_state" >&2
            return 1
            ;;
    esac
}