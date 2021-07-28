# `/init`
Minimal PID 1 for initramfs.

This is used for diskless servers with only two stages: `/etc/boot` and `/etc/reboot`.

For all spawned processes:
- inputs from `stdin` are dropped.
- outputs from `stdout` are redirected to `/dev/console`.
- logs from `stderr` are redirected to `/dev/kmsg`.

## Build & Install

The binary must be installed as `/init` and nothing else.

    $ make install DESTDIR=<PATH>

## Update & reboot

When your system is running with `/init` you don't need any extra tool to do an update.
Just put the new kernel at `/kernel` and `/init` will automatically kexec it after the `/etc/reboot` stage.

If you want to update your system in the usual way (pxe, efi,...) you need a cold reboot:

    $ touch /reboot
    $ reboot

With `reboot` a script that stops your supervisor. For example with `runit`:

    $ cat /bin/reboot
    #!/bin/sh

    exec pkill -HUP runsvdir

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

    # you can still use kexec-tools for old kernels or unsupported arch.
    kexec -l /kernel --reuse-cmdline && kexec -e

## File system

`/init` will create a minimal file system that should look like this if all goes well:

    # cat /proc/mounts
    none / rootfs rw,nosuid 0 0
    none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
    none /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0
    none /sys/fs/cgroup cgroup2 rw,nosuid,nodev,noexec,relatime 0 0
    none /dev devtmpfs rw,nosuid,noexec 0 0
    devpts /dev/pts devpts rw,nosuid,noexec,relatime,gid=5,mode=620,ptmxmode=000 0 0

Note that `/init` mounts `cgroup2` as `/sys/fs/cgroup` and not the old `cgroup`.

## Kernel configuration

A minimal kernel configuration to enable all features of `/init` would look like this:

    CONFIG_BLK_DEV_INITRD=y
    CONFIG_INITRAMFS_SOURCE=<PATH>  # root directory with <PATH>/init
    CONFIG_BINFMT_ELF=y
    CONFIG_BINFMT_SCRIPT=y
    CONFIG_DEVTMPFS=y
    CONFIG_PROC_FS=y
    CONFIG_SYSFS=y

Since all logs go into the kernel, it's a good idea to increase the size of the log buffer:

    CONFIG_LOG_BUF_SHIFT=24

Required for updates:

    CONFIG_RELOCATABLE=y
    CONFIG_CRYPTO=y
    CONFIG_CRYPTO_SHA256=y
    CONFIG_KEXEC_FILE=y

Not mandatory but useful to see what's going on :)

    CONFIG_PRINTK=y
    CONFIG_TTY=y
    CONFIG_SERIAL_8250=y
    CONFIG_SERIAL_8250_CONSOLE=y

---
For feature requests and bug reports,
please create an [issue](https://github.com/angt/slashinit/issues).
