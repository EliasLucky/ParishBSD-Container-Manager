#!/bin/sh
# tests/run.sh  ParishBSD Compartments test harness.
#
# Runs refusal-path tests for parish_jail_state and
# parish_require_not_contained, plus validator tests against the
# jail-names fixture.
#
# REQUIREMENTS:
#   - Must be run as root. Tests write to /var/run/parishbsd/ (state
#     file mocks) and /usr/local/jails/ (jail dir mocks for the
#     personality fixture). The state machine reads from
#     these paths and cannot be redirected without a major refactor
#     of state.sh. Running as root is the pragmatic choice for 4a.
#   - PARISH_TEST_JAIL=<running-jail>: required for `running`,
#     `running-other-contained`, and `contained-but-running` state
#     tests. If unset, those tests are skipped with a visible
#     "[SKIP]" line.
#   - PARISH_TEST_GUARDIAN=1: reserved for four-package
#     matrix.
#
# EXIT CODES:
#   0  all tests passed (skips allowed)
#   1  one or more tests failed
#   2  harness misconfiguration (not root, missing fixture, etc.)

set -eu

TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
export TESTS_DIR
export LIBDIR=/usr/local/libexec/parishbsd/lib
export FIXTURES_DIR="$TESTS_DIR/fixtures"

# Root check
[ "$(id -u)" -eq 0 ] || {
    printf 'tests/run.sh: must be run as root\n' >&2
    exit 2
}

# Fixtures present
for f in jail-names.txt personality-behavior.txt pkg-query-output.txt; do
    [ -f "$FIXTURES_DIR/$f" ] || {
        printf 'tests/run.sh: missing fixture: %s\n' "$FIXTURES_DIR/$f" >&2
        exit 2
    }
done

# Temp dir under the tool's runtime space
_RUNDIR=/var/run/parishbsd
mkdir -p "$_RUNDIR" 2>/dev/null || true
[ -d "$_RUNDIR" ] || {
    printf 'tests/run.sh: cannot create %s\n' "$_RUNDIR" >&2
    exit 2
}
export TEST_SCRATCH
TEST_SCRATCH=$(mktemp -d "$_RUNDIR/tests.XXXXXX") \
    || { printf 'tests/run.sh: mktemp failed\n' >&2; exit 2; }

_cleanup() {
    [ -n "${TEST_SCRATCH:-}" ] && [ -d "$TEST_SCRATCH" ] && rm -rf "$TEST_SCRATCH"
}
trap _cleanup EXIT

# Load assertion helpers
. "$TESTS_DIR/lib.sh"

# Counters
TEST_PASS=0
TEST_FAIL=0
TEST_SKIP=0

# Dispatch
_run_test_file() {
    _file="$1"
    printf '\n=== %s ===\n' "$(basename "$_file")"
    # shellcheck disable=SC1090
    . "$_file"
}

_run_test_file "$TESTS_DIR/test_validators.sh"
_run_test_file "$TESTS_DIR/test_jail_state.sh"
_run_test_file "$TESTS_DIR/test_require_not_contained.sh"

printf '\n=== Summary ===\n'
printf 'Passed: %d\n' "$TEST_PASS"
printf 'Failed: %d\n' "$TEST_FAIL"
printf 'Skipped: %d\n' "$TEST_SKIP"

[ "$TEST_FAIL" -eq 0 ] || exit 1
exit 0