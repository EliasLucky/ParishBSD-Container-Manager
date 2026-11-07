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

exit 0
