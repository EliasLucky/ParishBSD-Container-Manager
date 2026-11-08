#!/bin/sh
# provision.d/linux-enable.sh <staging-dataset-path>
#
# Verifies the host has the linux64 kernel module loaded.
set -eu

# refuse if the host is missing the module.
if ! kldstat -m linux64 >/dev/null 2>&1 && ! kldstat -m linux >/dev/null 2>&1 && ! sysctl -n kern.conftxt 2>/dev/null | grep -q 'COMPAT_LINUXKPI'; then
    printf 'parish: Linux compatibility requires the host kernel module\n' >&2
    printf '        "linux64". Load it with:\n' >&2
    printf '            kldload linux64\n' >&2
    printf '        Or add to the host'"'"'s /etc/rc.conf:\n' >&2
    printf '            linux_enable=YES\n' >&2
    printf '        And reboot.\n' >&2
    exit 1
fi
# /compat/linux is provided by the host (sysutils/linux_base-rl9).
# The apps-compatible template nullfs-mounts it read-only at jail
# start. Verify it exists before letting the template build succeed.
if [ ! -d /compat/linux/bin ] || [ ! -x /compat/linux/bin/sh ]; then
    printf 'parish: Linux userland missing on host.\n' >&2
    printf '        The apps-compatible template needs /compat/linux\n' >&2
    printf '        (from sysutils/linux_base-rl9) to be installed on\n' >&2
    printf '        this machine.\n' >&2
    printf '        Install it with:\n' >&2
    printf '            pkg install linux_base-rl9\n' >&2
    printf '        Then rebuild the template:\n' >&2
    printf '            pkexec parishctl template build apps-compatible\n' >&2
    exit 1
fi
exit 0
