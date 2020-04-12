# /init
Minimal PID 1 for initramfs

This is used for diskless servers with only two stages: `/etc/boot` and `/etc/reboot`.

If `/kernel` exists, /init will automatically kexec it after the reboot stage.

## Build & Install

The binary must be installed as `/init` and nothing else.

    $ make install DESTDIR=<PATH>

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

    # you can use kexec-tools for old kernels or unsupported arch.
    kexec -l /run/boot --reuse-cmdline && kexec -e
