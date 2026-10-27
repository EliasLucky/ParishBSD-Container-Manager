#!/bin/sh
# libexec/parishbsd/lib/validate.sh
#
# Validation primitives for ParishBSD Compartments.
#
# This library has NO dependencies on other ParishBSD libraries.
# It is the leaf; callers source it first.
#
# Callers must NOT rely on any shell option state this file sets,
# because it sets none. Specifically, there is no `set -eu` at file
# scope: that would propagate to the caller and cause a caller that
# relies on its own `set +u` to die unexpectedly. Every function
# guards its own required argument with [ -n "${1-}" ].
#
# The four validators split by purpose:
#
#   validate_jail_name           STRICT   for creation of new jails
#   validate_jail_name_existing  PERMISSIVE for existing jails; mirrors
#                                         Guardian's containment.cpp::
#                                         valid_jail_name so we can act
#                                         on any jail Guardian can contain
#   validate_personality_name    personality identifiers (shipped by the
#                                 port; narrower alphabet than jail names)
#   validate_app_name            application identifiers; shared by
#                                 parish-app-launch and
#                                 parish-app-create-launcher
#
# The fixture tests/fixtures/jail-names.txt pins all three jail-name
# validators against each other. Do not change the rules here without
# updating the fixture.

# --------------------------------------------------------------------
# Length constants
# --------------------------------------------------------------------

# Not readonly: test harnesses may override these to exercise boundary
# cases. If a caller sets them to a nonsense value, the fixture catches
# it.

# 31 is a usability choice, not a technical limit.
# Justification: names longer than ~32 chars truncate in ps/top output
# on narrow terminals, which violates observability. Guardian's
# own validator (containment.cpp::valid_jail_name) allows up to 64.
# Creation-time policy may be stricter; runtime tolerance must not.
_JAIL_NAME_STRICT_MAX=31
_JAIL_NAME_EXISTING_MAX=64

# --------------------------------------------------------------------
# Jail names
# --------------------------------------------------------------------

# validate_jail_name <name>
# STRICT: creation-time policy.
#
# Rejects:
#   - empty names
#   - names longer than _JAIL_NAME_STRICT_MAX (31) characters
#   - names containing characters outside [a-z0-9-]
#   - names starting with a hyphen
#   - names ending with a hyphen
#   - names containing consecutive hyphens
#
# The character-class check runs first so the most actionable error
# is reported first: a name like a--B gets "may only contain a-z, 0-9,
# and hyphen" rather than the structural "consecutive hyphens" message.
#
# On failure, prints a message to stderr and returns 1.
validate_jail_name() {
    _n="${1-}"
    [ -n "$_n" ] || { printf 'parish: jail name is empty\n' >&2; return 1; }
    [ "${#_n}" -le "$_JAIL_NAME_STRICT_MAX" ] || {
        printf 'parish: jail name "%s" longer than %d characters\n' \
            "$_n" "$_JAIL_NAME_STRICT_MAX" >&2
        return 1
    }
    case "$_n" in
        *[!a-z0-9-]*)
            printf 'parish: jail name may only contain a-z, 0-9, and hyphen\n' >&2
            return 1 ;;
        -*)    printf 'parish: jail name may not start with a hyphen\n' >&2; return 1 ;;
        *-)    printf 'parish: jail name may not end with a hyphen\n' >&2; return 1 ;;
        *--*)  printf 'parish: jail name may not contain consecutive hyphens\n' >&2; return 1 ;;
    esac
    return 0
}

# validate_jail_name_existing <name>
# PERMISSIVE: mirrors guardian/src/containment.cpp::valid_jail_name.
#
# Every name Guardian's valid_jail_name accepts must be accepted here.
# If we refuse a jail Guardian contains, we cannot act on the incident;
# that would be a bug. Creation-time policy may be stricter (see
# validate_jail_name); runtime tolerance must not.
#
# Accepts: uppercase, lowercase, digits, "_", ".", "-", up to 64 chars.
# Rejects: empty; longer than 64; leading "."; containing "..";
#          any character outside [A-Za-z0-9._-].
#
# Note: the permissive validator ACCEPTS names like "-a" and "a-" that
# the strict validator rejects. That is intentional — a jail named
# "-a" can exist (Guardian allows it), can be contained, can be listed;
# only creation of a new "-a" is refused. See parishctl --help under
# CREATING for the user-facing note.
validate_jail_name_existing() {
    _n="${1-}"
    [ -n "$_n" ] || { printf 'parish: jail name is empty\n' >&2; return 1; }
    [ "${#_n}" -le "$_JAIL_NAME_EXISTING_MAX" ] || {
        printf 'parish: jail name "%s" longer than %d characters\n' \
            "$_n" "$_JAIL_NAME_EXISTING_MAX" >&2
        return 1
    }
    case "$_n" in
        .*)                printf 'parish: jail name may not start with a dot\n' >&2; return 1 ;;
        *..*)              printf 'parish: jail name may not contain ".."\n' >&2; return 1 ;;
        *[!A-Za-z0-9._-]*) printf 'parish: jail name contains invalid characters\n' >&2; return 1 ;;
    esac
    return 0
}

# --------------------------------------------------------------------
# Personality names
# --------------------------------------------------------------------

# validate_personality_name <name>
#
# Personalities are shipped by the port (personalities/<name>/). The
# alphabet is narrower than jail names because personality names are
# never user-chosen - they are directory names in a package the user
# installed.
#
# Accepts: [a-z0-9-]+, non-empty, no leading hyphen.
# Rejects: empty; leading hyphen; any "/" or ".."; any character
#          outside [a-z0-9-].
#
# The ".." rejection prevents a compromised personality directory
# from causing path traversal when the name is used to build a path
# like "$PERSONALITIES/$personality".
validate_personality_name() {
    _p="${1-}"
    [ -n "$_p" ] || { printf 'parish: personality name is empty\n' >&2; return 1; }
    case "$_p" in
        -*)
            printf 'parish: personality name may not start with a hyphen\n' >&2
            return 1 ;;
        */*|*..*)
            printf 'parish: personality name may not contain a path\n' >&2
            return 1 ;;
        *[!a-z0-9-]*)
            printf 'parish: personality name may only contain a-z, 0-9, and hyphen\n' >&2
            return 1 ;;
    esac
    return 0
}
# --------------------------------------------------------------------
# Package names
# --------------------------------------------------------------------

# validate_package_name <name>
#
# Package names appear in:
#   - pkg(8) command argv: `pkg -j <jail> install -y <pkg>`
#   - in-jail .desktop filenames: `<pkg>.desktop`
#   - metadata paths in launcher.sh (slug=<pkg>)
#
# Same character class as validate_app_name (the two are semantically
# different but the charset is identical
# and both can reach argument positions in pkg(8), jexec, and grep
# argv). Rejects leading `-` before the charset check (a package name
# starting with `-` reaches pkg as an option, not a name).
validate_package_name() {
    _p="${1-}"
    [ -n "$_p" ] || { printf 'parish: package name is empty\n' >&2; return 1; }
    [ "${#_p}" -le 64 ] || {
        printf 'parish: package name longer than 64 characters\n' >&2
        return 1
    }
    case "$_p" in
        -*)
            printf 'parish: package name may not start with -\n' >&2
            return 1 ;;
        *[!A-Za-z0-9._+-]*)
            printf 'parish: package name contains invalid characters\n' >&2
            return 1 ;;
    esac
    unset _p
    return 0
}
# --------------------------------------------------------------------
# Application names
# --------------------------------------------------------------------

# validate_app_name <name>
#
# Application names are user-chosen identifiers for apps inside a
# compartment. They appear in:
#   - host-side metadata filenames (/var/db/parishbsd/launchers/<jail>/<name>.conf)
#   - .desktop Exec= arguments (quoted, escaped)
#   - argv elements to parish-jail-run (quoted)
#   - argv[2] to parish-app-launch
#
# A single set of rules shared by parish-app-launch and
# parish-app-create-launcher (single source of truth). If
# the two diverged, parish-app-create-launcher would create launchers
# parish-app-launch refuses, and the user would see a broken shortcut
# with no visible cause.
#
# Stricter than the .desktop spec (spaces are legal there) because the
# identifiers are also user-facing and easier to type without spaces.
# The .desktop Name= field is separate and can contain spaces; only
# the identifier is restricted.
#
# Accepts: [A-Za-z0-9._+-]+, up to 64 chars, non-empty, not starting
#          with a dot, no "..", no "/".
# Rejects: empty; over 64 chars; leading dot; containing "..";
#          containing "/"; any character outside [A-Za-z0-9._+-].
validate_app_name() {
    _a="${1-}"
    [ -n "$_a" ] || { printf 'parish: application name is empty\n' >&2; return 1; }
    [ "${#_a}" -le 64 ] || {
        printf 'parish: application name longer than 64 characters\n' >&2
        return 1
    }
    case "$_a" in
        .*)
            printf 'parish: application name may not start with a dot\n' >&2
            return 1 ;;
        *..*)
            printf 'parish: application name may not contain ".."\n' >&2
            return 1 ;;
        */*)
            printf 'parish: application name may not contain "/"\n' >&2
            return 1 ;;
        *[!A-Za-z0-9._+-]*)
            printf 'parish: application name may only contain letters, digits, ".", "_", "+", "-"\n' >&2
            return 1 ;;
    esac
    return 0
}