#!/bin/sh
# libexec/parishbsd/lib/service.sh
#
# The single `service jail start` / `service jail stop` call site.
# Any other site is a bug (item 118).
#
# REQUIRES (sourced by caller, in this order):
#   validate.sh     validate_jail_name_existing (re-validated here as
#                   defense in depth  the caller validates, but a
#                   future caller that forgets must not pass a
#                   malformed name to jail(8))
#   state.sh        parish_jail_state, parish_jail_state_message
#   err, log        provided by the caller; the standard failure/INFO
#                   helpers defined by parishctl-create, parishctl-start,
#                   and any other caller in this tree
#   service.sh      this file
#
# Failure model:
#   - return 1 for service failures (exit code, non-empty stderr on
#     start, state mismatch). The caller has a recovery block that
#     names the next step; returning 1 lets it run.
#   - err for setup failures (mkdir, chmod, lock open, lock timeout,
#     mktemp). The caller cannot recover; exiting the shell is the
#     right behavior.
#
# Patterns:
#   start: exit code checked; stderr checked with -s (any non-empty
#          stderr is a refusal, no pattern. No whitelist: verified 0-byte stderr on success
#          on FreeBSD 15.
#   stop:  exit code checked; stderr check PROVISIONAL pending
#          `service jail stop <running-jail> 2>/tmp/err; wc -c /tmp/err`.
#          If that returns 0, stop's pattern matches start's. If >0,
#          stop is exit-code-only and this header records the deviation.
#
# Post-condition: after the service call, parish_jail_state must show
# the expected state; otherwise err.
#
# Lock: /var/run/parishbsd/service.lock, flock -w 30. The bounded wait
# protects against the lock itself becoming a DoS vector. Truncation on
# open (`9>`) is idiomatic; no content is ever written.
#
# No EXIT trap: an EXIT trap in this file would clobber the caller's
# trap. Every error path does explicit cleanup.

parish_service_start() {
    _ps_jail="${1:?parish_service_start: jail name required}"
    _ps_jailroot="${2:?parish_service_start: jailroot required}"

    validate_jail_name_existing "$_ps_jail" || return 1

    mkdir -p /var/run/parishbsd || err "cannot create /var/run/parishbsd"
    chmod 0700 /var/run/parishbsd || err "cannot chmod 0700 /var/run/parishbsd"

    if [ -d "$_ps_jailroot/xpra" ]; then
        mkdir -p "/var/run/xpra/$_ps_jail" || err "cannot create /var/run/xpra/$_ps_jail"
        chmod 0700 "/var/run/xpra/$_ps_jail" || err "cannot chmod 0700 /var/run/xpra/$_ps_jail"
    fi

    exec 9> /var/run/parishbsd/service.lock || err "cannot open service lock"
    flock -w 30 9 || { exec 9>&-; err "could not acquire service lock (another create/start in progress)"; }

    _ps_err=$(mktemp /var/run/parishbsd/svc-err.XXXXXX) \
        || { exec 9>&-; err "cannot create temp for service output"; }

    _ps_rc=0
    service jail start "$_ps_jail" 2>"$_ps_err" || _ps_rc=$?

    if [ "$_ps_rc" -ne 0 ]; then
        if [ -s "$_ps_err" ]; then
            echo "The container service reported an error while starting:" >&2
            sed 's/^/  /' "$_ps_err" >&2
            echo "" >&2
        fi
        rm -f "$_ps_err"
        exec 9>&-
        return 1
    fi

    if [ -s "$_ps_err" ]; then
        echo "The container service reported output on stderr while starting:" >&2
        sed 's/^/  /' "$_ps_err" >&2
        echo "" >&2
        echo "The container's state is not trustworthy." >&2
        rm -f "$_ps_err"
        exec 9>&-
        return 1
    fi
    rm -f "$_ps_err"

    _ps_state=$(parish_jail_state "$_ps_jail")
    case "$_ps_state" in
        running|running-other-contained) : ;;
        *)
            exec 9>&-
            parish_jail_state_message "$_ps_jail" "$_ps_state" >&2
            return 1
            ;;
    esac

    chmod 0600 "/var/log/jail_${_ps_jail}_console.log" 2>/dev/null || true

    exec 9>&-
    return 0
}

parish_service_stop() {
    _ps_jail="${1:?parish_service_stop: jail name required}"

    validate_jail_name_existing "$_ps_jail" || return 1

    exec 9> /var/run/parishbsd/service.lock || err "cannot open service lock"
    flock -w 30 9 || { exec 9>&-; err "could not acquire service lock (another create/stop in progress)"; }

    _ps_rc=0
    service jail stop "$_ps_jail" || _ps_rc=$?
    if [ "$_ps_rc" -ne 0 ]; then
        exec 9>&-
        return 1
    fi

    _ps_state=$(parish_jail_state "$_ps_jail")
    case "$_ps_state" in
        stopped) : ;;
        *)
            exec 9>&-
            return 1
            ;;
    esac

    exec 9>&-
    return 0
}