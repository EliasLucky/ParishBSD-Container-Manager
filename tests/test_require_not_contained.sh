#!/bin/sh
# tests/test_require_not_contained.sh  refusal-path tests for
# parish_require_not_contained. Sourced by run.sh.

. "$LIBDIR/state.sh"

_JAIL="rnc_test_$$"
_OTHER="rnc_other_$$"

_cleanup_rnc() {
    clean_parish_state
    rm -rf "/usr/local/jails/app/$_JAIL" 2>/dev/null || true
    rm -rf "/usr/local/jails/app/$_OTHER" 2>/dev/null || true
}
trap _cleanup_rnc EXIT

# --- allowed: running (needs PARISH_TEST_JAIL) ---
if [ -n "${PARISH_TEST_JAIL:-}" ]; then
    if parish_require_not_contained "$PARISH_TEST_JAIL" 2>/dev/null; then
        _pass "rnc_allows_running"
    else
        _fail "rnc_allows_running" "returned non-zero for running jail"
    fi
else
    _skip rnc_allows_running "PARISH_TEST_JAIL not set"
fi

# --- allowed: stopped ---
mock_jail_dir app "$_JAIL" >/dev/null
if parish_require_not_contained "$_JAIL" 2>/dev/null; then
    _pass "rnc_allows_stopped"
else
    _fail "rnc_allows_stopped" "returned non-zero for stopped jail"
fi

# --- refused: contained ---
mock_detection_conf \
"[incident]
level=contained
forensic_snapshot=ok
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$_JAIL"
if parish_require_not_contained "$_JAIL" 2>/dev/null; then
    _fail "rnc_refuses_contained" "accepted a contained jail"
else
    _pass "rnc_refuses_contained"
fi

# --- refused: containment-failed ---
mock_detection_conf \
"[incident]
level=containment-failed
forensic_snapshot=failed
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$_JAIL"
if parish_require_not_contained "$_JAIL" 2>/dev/null; then
    _fail "rnc_refuses_containment_failed" "accepted a containment-failed jail"
else
    _pass "rnc_refuses_containment_failed"
fi

# --- refused: containing ---
mock_incident_conf "1700000000000000000" \
"[incident]
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$_JAIL"
if parish_require_not_contained "$_JAIL" 2>/dev/null; then
    _fail "rnc_refuses_containing" "accepted a containing jail"
else
    _pass "rnc_refuses_containing"
fi
rm -f /var/run/parishbsd/incident.*.conf

# --- refused: empty name ---
_err=$(parish_require_not_contained "" 2>&1) && _rc=0 || _rc=1
assert_rc_eq "rnc_refuses_empty_name_rc" 1 "$_rc"
assert_contains "rnc_refuses_empty_name_msg" "jail name required" "$_err"

# --- refused: invalid name ---
_err=$(parish_require_not_contained "a..b" 2>&1) && _rc=0 || _rc=1
assert_rc_eq "rnc_refuses_invalid_name_rc" 1 "$_rc"

# --- refused: unknown (no dir, no state) ---
clean_parish_state
if parish_require_not_contained "$_JAIL" 2>/dev/null; then
    # Note: unknown jail dir returns "unknown" from parish_jail_state
    # but parish_require_not_contained returns 1 (unknown not in allow list)
    _fail "rnc_refuses_unknown" "accepted an unknown-state jail"
else
    _pass "rnc_refuses_unknown"
fi

_cleanup_rnc
trap - EXIT