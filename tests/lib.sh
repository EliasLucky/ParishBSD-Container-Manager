#!/bin/sh
# tests/lib.sh  assertion helpers for the ParishBSD Compartments
# test harness. Sourced by tests/run.sh after set -eu and root check.
#
# Requires (from run.sh):
#   TESTS_DIR, FIXTURES_DIR, LIBDIR, TEST_SCRATCH, TEST_PASS,
#   TEST_FAIL, TEST_SKIP (incremented by these helpers)

# _pass <name>
_pass() {
    TEST_PASS=$((TEST_PASS + 1))
    printf '[PASS] %s\n' "$1"
}

# _fail <name> <message>
_fail() {
    TEST_FAIL=$((TEST_FAIL + 1))
    printf '[FAIL] %s: %s\n' "$1" "$2" >&2
}

# _skip <name> <reason>
_skip() {
    TEST_SKIP=$((TEST_SKIP + 1))
    printf '[SKIP] %s: %s\n' "$1" "$2"
}

# assert_eq <name> <expected> <actual>
assert_eq() {
    if [ "$2" = "$3" ]; then
        _pass "$1"
    else
        _fail "$1" "expected '$2', got '$3'"
    fi
}

# assert_rc_eq <name> <expected_rc> <actual_rc>
assert_rc_eq() {
    if [ "$2" -eq "$3" ]; then
        _pass "$1"
    else
        _fail "$1" "expected rc $2, got rc $3"
    fi
}

# assert_contains <name> <needle> <haystack>
assert_contains() {
    case "$3" in
        *"$2"*) _pass "$1" ;;
        *)      _fail "$1" "output does not contain '$2'" ;;
    esac
}

# fixture_lines <fixture> — yield non-blank, non-comment lines
fixture_lines() {
    grep -v '^#' "$1" | grep -v '^[[:space:]]*$' || true
}

# mock_jail_dir <tier> <name> — create a jail directory
mock_jail_dir() {
    _tier="$1"
    _name="$2"
    _dir="/usr/local/jails/$_tier/$_name"
    mkdir -p "$_dir"
    printf '%s\n' "$_dir"
}

# mock_metadata <jailroot> <personality>
mock_metadata() {
    mkdir -p "$1/usr/local/etc/parishbsd"
    printf 'PERSONALITY=%s\nTIER=%s\n' "$2" "$(basename "$(dirname "$1")")" \
        > "$1/usr/local/etc/parishbsd/metadata"
    chmod 0644 "$1/usr/local/etc/parishbsd/metadata"
    chown root:wheel "$1/usr/local/etc/parishbsd/metadata"
}

# mock_detection_conf <content>  write detection.conf atomically.
# Content must match respond_main.cpp::build_detection_file output
# shape: [incident] block with jail=, rule_id=, severity=,
# timestamp_wall=, level=, forensic_snapshot=; [compartment] block;
# [action] blocks.
mock_detection_conf() {
    _tmp="$TEST_SCRATCH/detection.conf.tmp.$$"
    printf '%s\n' "$1" > "$_tmp"
    chmod 0600 "$_tmp"
    chown root:wheel "$_tmp"
    mv -f "$_tmp" /var/run/parishbsd/detection.conf
}

# mock_incident_conf <arrival_ns> <content>
mock_incident_conf() {
    _tmp="$TEST_SCRATCH/incident.$1.conf.tmp.$$"
    printf '%s\n' "$2" > "$_tmp"
    chmod 0600 "$_tmp"
    chown root:wheel "$_tmp"
    mv -f "$_tmp" "/var/run/parishbsd/incident.$1.conf"
}

# clean_parish_state  remove all mocks the harness may have written
clean_parish_state() {
    rm -f /var/run/parishbsd/detection.conf
    rm -f /var/run/parishbsd/incident.*.conf 2>/dev/null || true
    rm -rf /var/run/parishbsd/released 2>/dev/null || true
}