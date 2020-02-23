# slashinit
Minimal PID 1 for initramfs

This is used for diskless servers with ONLY two states: running and upgrading.

## Configuration

Only two files are needed, for example:

    $ cat /etc/boot
    #!/bin/sh

    # your personal stuff..

    exec runsvdir -P /run/service

and

    $ cat /etc/reboot
    #!/bin/sh

    # your personal stuff..

    kexec -l /run/boot --reuse-cmdline && kexec -e
