#!/bin/sh
# tests/test_jail_state.sh  refusal-path tests for parish_jail_state.
# Sourced by run.sh.
#
# Mocks state files per fixture design:
#   - detection.conf format: [incident] block per
#     respond_main.cpp::build_detection_file.
#   - incident.*.conf format: per watch_main.cpp::write_incident.
#   - released/<jail> format: per state.sh layer 2 (Tranche 2b).

. "$LIBDIR/state.sh"

_JAIL="test_$$"
_OTHER="other_$$"

_cleanup_jail_state() {
    clean_parish_state
    rm -rf "/usr/local/jails/app/$_JAIL" 2>/dev/null || true
    rm -rf "/usr/local/jails/app/$_OTHER" 2>/dev/null || true
}
trap _cleanup_jail_state EXIT

# --- state_unknown: no jail dir, no state files ---
clean_parish_state
_test_name="state_unknown_clean_slate"
_state=$(parish_jail_state "$_JAIL")
assert_eq "$_test_name" "unknown" "$_state"

# --- state_stopped: jail dir, no state files ---
mock_jail_dir app "$_JAIL" >/dev/null
_test_name="state_stopped"
_state=$(parish_jail_state "$_JAIL")
assert_eq "$_test_name" "stopped" "$_state"

# --- state_containing: incident.*.conf mentions this jail ---
mock_incident_conf "1700000000000000000" \
"[incident]
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$_JAIL"
_test_name="state_containing"
_state=$(parish_jail_state "$_JAIL")
assert_eq "$_test_name" "containing" "$_state"
rm -f /var/run/parishbsd/incident.*.conf

# --- state_contained: detection.conf for this jail, stopped ---
mock_detection_conf \
"[incident]
level=contained
forensic_snapshot=ok
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$_JAIL
[compartment]
name=$_JAIL
type=Application
status=triggered"
_test_name="state_contained"
_state=$(parish_jail_state "$_JAIL")
assert_eq "$_test_name" "contained" "$_state"

# --- state_containment_failed ---
mock_detection_conf \
"[incident]
level=containment-failed
forensic_snapshot=failed
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$_JAIL"
_test_name="state_containment_failed"
_state=$(parish_jail_state "$_JAIL")
assert_eq "$_test_name" "containment-failed" "$_state"

# --- state_stopped_maybe_contained: detection.conf for other jail ---
mock_detection_conf \
"[incident]
level=contained
forensic_snapshot=ok
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$_OTHER"
mock_jail_dir app "$_OTHER" >/dev/null
_test_name="state_stopped_maybe_contained"
_state=$(parish_jail_state "$_JAIL")
assert_eq "$_test_name" "stopped-maybe-contained" "$_state"

# --- state_released: released marker exists ---
clean_parish_state
mkdir -p /var/run/parishbsd/released
printf 'released_at=2026-10-27T12:00:00Z\nreleased_by=root\nreason=test\n' \
    > "/var/run/parishbsd/released/$_JAIL"
chmod 0644 "/var/run/parishbsd/released/$_JAIL"
_test_name="state_released"
_state=$(parish_jail_state "$_JAIL")
assert_eq "$_test_name" "stopped" "$_state"

# --- Tests requiring a running jail ---
if [ -z "${PARISH_TEST_JAIL:-}" ]; then
    _skip state_running "PARISH_TEST_JAIL not set"
    _skip state_contained_but_running "PARISH_TEST_JAIL not set"
    _skip state_running_other_contained "PARISH_TEST_JAIL not set"
else
    # state_running
    clean_parish_state
    _test_name="state_running"
    _state=$(parish_jail_state "$PARISH_TEST_JAIL")
    assert_eq "$_test_name" "running" "$_state"

    # state_contained_but_running
    mock_detection_conf \
"[incident]
level=contained
forensic_snapshot=ok
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$PARISH_TEST_JAIL"
    _test_name="state_contained_but_running"
    _state=$(parish_jail_state "$PARISH_TEST_JAIL")
    assert_eq "$_test_name" "contained-but-running" "$_state"

    # state_running_other_contained
    mock_detection_conf \
"[incident]
level=contained
forensic_snapshot=ok
timestamp_wall=2026-10-27T12:00:00Z
severity=critical
rule_id=write_host_binaries
summary=test
jail=$_OTHER"
    mock_jail_dir app "$_OTHER" >/dev/null
    _test_name="state_running_other_contained"
    _state=$(parish_jail_state "$PARISH_TEST_JAIL")
    assert_eq "$_test_name" "running-other-contained" "$_state"
fi

_cleanup_jail_state
trap - EXIT