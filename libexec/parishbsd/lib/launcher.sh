#!/bin/sh
# libexec/parishbsd/lib/launcher.sh
#
# Host-side launcher metadata writer. The single implementation used
# by the sanctioned writers:
#
#   - parish-apps install         (catalog install, writes
#                                  metadata immediately after
#                                  `pkg install`, closing the
#                                  extraction window for the primary
#                                  install path)
#   - parish-apps install-file    (file install writes metadata
#                                  after the install handler
#                                  completes, still within the small
#                                  window between install and
#                                  extraction)
#   - parish-app-create-launcher  (Case 2 writes metadata from
#                                  in-jail .desktop extraction when
#                                  host-side metadata is absent.
#                                  This is the manual-pkg-install
#                                  path; extraction is jail-influenced
#                                  and the header of that binary
#                                  documents the residual risk.)
#
# No other code path may write to /var/db/parishbsd/launchers/
#
# REQUIRES (sourced by caller, in this order):
#   validate.sh    validate_jail_name_existing, validate_app_name
#   launcher.sh    this file
#
# Format matches exactly:
#   # header comment
#   # header comment
#   jail=<jail>
#   slug=<slug>
#   name=<display name>
#   version=<version>
#   method=<install method>
#   install_path=<path>
#   icon=<icon name or empty>
#   written_at=<date -u +%Y-%m-%dT%H:%M:%SZ>
#   ARGV[0]="..."
#   ARGV[1]="..."
#
# Any change to this format requires coordinated change to
# parish-app-launch's parser. The parser ignores unknown
# header keys, so adding a header field (like icon=) is backward
# compatible  but removing or renaming a field is not.

# write_host_launcher_metadata <jail> <slug> <name> <version> <method> <install_path> <icon> <argv0> [argv1 ...]
#
# icon: empty string for "no specific icon" (the .desktop writer
#       falls back to a generic). A non-empty icon must be a plain
#       name (no path separators, no ..). This is validated here, not
#       by the caller, because the value reaches the .desktop Icon=
#       line, which some file managers treat as a path.
write_host_launcher_metadata() {
    # verify LAUNCHER_STORE is set and is the
    # expected path. A caller that forgets to set it, or sets it
    # wrong, would write to an unexpected location.
    #
    # Note: the hardcoded expected-path check prevents a future
    # caller from legitimately overriding LAUNCHER_STORE (e.g. for
    # tests). If that becomes needed, the check should be relaxed to
    # a prefix check (`case "$LAUNCHER_STORE" in /var/db/*)`) rather
    # than removed.
    [ -n "${LAUNCHER_STORE:-}" ] || err "LAUNCHER_STORE is not set (caller bug)"
    case "$LAUNCHER_STORE" in
        /var/db/parishbsd/launchers) : ;;
        *) err "LAUNCHER_STORE is '$LAUNCHER_STORE' (expected /var/db/parishbsd/launchers)" ;;
    esac

    _jail="$1"
    _slug="$2"
    _name="$3"
    _version="$4"
    _method="$5"
    _install_path="$6"
    _icon="$7"
    shift 7

    [ "$#" -ge 1 ] || err "at least one argv element required"

    validate_jail_name_existing "$_jail"
    validate_app_name "$_slug" || err "invalid slug: '$_slug'"

    # Icon name validation:
    # plain name, not a path. The value is written into the metadata
    # and read into the .desktop Icon= line, which some file managers
    # treat as a path. A path-like value could escape the icon
    # directory.
    case "$_icon" in
        '')                     : ;;   # no icon: fallback to generic
        */*|*..*)               err "launcher icon must be a plain name, not a path: '$_icon'" ;;
        *[!A-Za-z0-9._+-]*)     err "launcher icon contains invalid characters: '$_icon'" ;;
    esac

    # Reject control characters in any header value.
    for _hdr_val in "$_name" "$_version" "$_method" "$_install_path" "$_icon"; do
        case "$_hdr_val" in
            *[[:cntrl:]]*)
                err "launcher metadata value contains a control character; refusing to write"
                ;;
        esac
    done

    _launcher_dir="$LAUNCHER_STORE/$_jail"
    install -d -m 0755 -o root -g wheel "$LAUNCHER_STORE" 2>/dev/null || true
    [ -d "$LAUNCHER_STORE" ] || err "cannot create $LAUNCHER_STORE (should be created by the port)"
    install -d -m 0755 -o root -g wheel "$_launcher_dir" 2>/dev/null || true
    [ -d "$_launcher_dir" ] || err "cannot create $_launcher_dir"

    _meta_file="$_launcher_dir/$_slug.conf"
    _tmp="${_meta_file}.tmp.$$"

    # validate argv BEFORE opening the
    # temp file. A validation failure inside the redirect block
    # would leave .tmp orphaned (err exits mid-write, redirect
    # target remains).
    _i=0
    for _arg in "$@"; do
        case "$_arg" in
            *[[:cntrl:]]*)
                err "launcher argv[$_i] contains a control character; refusing to write"
                ;;
            *'"'*)
                err "launcher argv[$_i] contains '\"'; refusing to write"
                ;;
        esac
        _i=$((_i + 1))
    done

    # EXIT trap for the write sequence.
    # Defense against failure between open and rename (chmod,
    # chown, or mv failing would otherwise leave .tmp orphaned).
    # EXIT-only; the sequence is short and
    # the trap is removed after the rename.
    trap 'rm -f "$_tmp"' EXIT

    {
        printf '# ParishBSD launcher metadata - written by parish-apps install-file\n'
        printf '# or parish-app-create-launcher. Do not edit.\n'
        printf 'jail=%s\n'         "$_jail"
        printf 'slug=%s\n'         "$_slug"
        printf 'name=%s\n'         "$_name"
        printf 'version=%s\n'      "$_version"
        printf 'method=%s\n'       "$_method"
        printf 'install_path=%s\n' "$_install_path"
        printf 'icon=%s\n'         "$_icon"
        printf 'written_at=%s\n'   "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        _i=0
        for _arg in "$@"; do
            printf 'ARGV[%d]="%s"\n' "$_i" "$_arg"
            _i=$((_i + 1))
        done
    } > "$_tmp"

    chmod 0644 "$_tmp"
    chown root:wheel "$_tmp"
    mv -f "$_tmp" "$_meta_file"
    trap - EXIT
    log "Wrote launcher metadata: $_meta_file"
}