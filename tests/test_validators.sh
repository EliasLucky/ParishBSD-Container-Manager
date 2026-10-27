#!/bin/sh
# tests/test_validators.sh  validator tests against jail-names.txt.
# Sourced by run.sh. Not a standalone script.

. "$LIBDIR/validate.sh"

# _expect_validator <fn_name> <input> <expected> <row_note>
_expect_validator() {
    _fn="$1"
    _in="$2"
    _exp="$3"
    _note="$4"
    if "$_fn" "$_in" 2>/dev/null; then
        _actual="accept"
    else
        _actual="reject"
    fi
    _test_name="validator_${_fn}_${_note}"
    assert_eq "$_test_name" "$_exp" "$_actual"
}

# Row loop
while IFS="$(printf '\t')" read -r input strict existing cpp note; do
    _expect_validator validate_jail_name          "$input" "$strict"   "$note"
    _expect_validator validate_jail_name_existing "$input" "$existing" "$note"

    # Item 118 agreement: existing and cpp must agree
    if [ "$existing" = "$cpp" ]; then
        _pass "agreement_${note}"
    else
        _fail "agreement_${note}" "existing='$existing' but cpp='$cpp'"
    fi
done <<EOF
$(fixture_lines "$FIXTURES_DIR/jail-names.txt")
EOF

# Special: real newline in input
_newline_input=$(printf 'a\na')
if validate_jail_name "$_newline_input" 2>/dev/null; then
    _fail validator_newline "strict validator accepted newline input"
else
    _pass validator_newline
fi
if validate_jail_name_existing "$_newline_input" 2>/dev/null; then
    _fail validator_newline_existing "permissive validator accepted newline input"
else
    _pass validator_newline_existing
fi