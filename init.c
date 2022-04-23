#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>

#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

char **__environ;

enum si_lvl {
    si_critical = 2,
    si_error    = 3,
    si_warning  = 4,
    si_info     = 6,
};

static void
si_log(enum si_lvl lvl, const char *fmt, ...)
{
    va_list ap;
    char tmp[256] = "<*>/init: ";
    char *buf = &tmp[10];
    size_t size = sizeof(tmp) - 10;

    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    if (ret <= 0)
        return;

    if (size > (size_t)ret)
        size = (size_t)ret;

    tmp[1] = '0' + (lvl & 7);
    (void)!write(2, tmp, size + 10);
}

static void
si_spawn(char *argv[], sigset_t *sig)
{
    si_log(si_info, "Running %s", argv[0]);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setsigmask(&attr, sig);
    posix_spawnattr_setflags(&attr,
            POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK);

    pid_t pid;
    int err = posix_spawn(&pid, argv[0], 0, &attr, argv, __environ);
    posix_spawnattr_destroy(&attr);

    if (err) {
        si_log(si_error, "Failed to run %s", argv[0]);
        return;
    }
    while (1) {
        int status;
        pid_t ret = waitpid(-1, &status, 0);

        if (ret == (pid_t)-1) {
            if (errno == EINTR)
                continue;
            si_log(si_error, "waitpid: %m");
            return;
        }
        if (ret == pid) {
            if (WIFEXITED(status) || WIFSIGNALED(status))
                return;
        }
    }
}

static void
si_mount(const char *src, const char *dst, const char *type,
         unsigned long flags, const void *data)
{
    mkdir(dst, 0755);

    if (mount(src, dst, type, flags, data))
        si_log(si_critical, "mount(%s): %m", dst);
}

static void
si_init_fs(void)
{
    (void)!chdir("/");

#define MS_NOSE MS_NOSUID | MS_NOEXEC
    si_mount(NULL, "/", NULL, MS_NOSUID | MS_REMOUNT, NULL);
    si_mount("none", "/proc", "proc", MS_NOSE | MS_NODEV, NULL);
    si_mount("none", "/sys", "sysfs", MS_NOSE | MS_NODEV, NULL);
    si_mount("none", "/sys/fs/cgroup", "cgroup2", MS_NOSE | MS_NODEV, NULL);
    si_mount("none", "/dev", "devtmpfs", MS_NOSE | MS_STRICTATIME, NULL);
    si_mount("devpts", "/dev/pts", "devpts", MS_NOSE, "gid=5,mode=0620");
#undef MS_NOSE

    mkdir("/dev/shm", 01777);
    mkdir("/tmp", 01777);

    (void)!symlink("/proc/mounts", "/etc/mtab");
    (void)!symlink("/proc/self/fd", "/dev/fd");
    (void)!symlink("/proc/self/fd/0", "/dev/stdin");
    (void)!symlink("/proc/self/fd/1", "/dev/stdout");
    (void)!symlink("/proc/self/fd/2", "/dev/stderr");

    unlink("/init");
}

static void
si_init_fd(const char *path, int newfd)
{
    int fd = open(path, O_RDWR | O_NONBLOCK | O_NOCTTY);

    if (fd < 0) {
        si_log(si_error, "open(%s): %m", path);
        return;
    }
    if (fd == newfd)
        return;

    dup2(fd, newfd);
    close(fd);
}

static void
si_init_term(void)
{
    if (getenv("TERM"))
        return;

    putenv("TERM=linux");
}

static void
si_clear_str(char *str)
{
    if (str) while (*str)
        *str++ = 0;
}

static ssize_t
si_read_file(const char *file, char *buf, size_t size)
{
    int fd = open(file, O_RDONLY);

    if (fd < 0)
        return 0;

    ssize_t ret = read(fd, buf, size);
    int tmp_errno = errno;
    close(fd);
    errno = tmp_errno;

    if (ret < 0) {
        si_log(si_error, "read(%s): %m", file);
        return -1;
    }
    return ret;
}

static void
si_reboot(int cmd)
{
    si_log(si_info, "Syncing...");
    sync();

    si_log(si_info, "Killing all processes...");
    kill(-1, SIGKILL);

    si_log(si_info, "Rebooting...");

    if (reboot(cmd))
        si_log(si_error, "reboot: %m");
}

static void
si_update(const char *kernel)
{
    char cmd[1024] = {0};

    ssize_t len = si_read_file("/kernel.cmdline", cmd, sizeof(cmd) - 1);

    if (len < 0)
        return;

    int fd = open(kernel, O_RDONLY);

    if (fd < 0) {
        if (errno != ENOENT)
            si_log(si_error, "open(%s): %m", kernel);
        return;
    }
#ifdef SYS_kexec_file_load
    si_log(si_info, "Found %s, loading...", kernel);

    if (syscall(SYS_kexec_file_load, fd, -1, (unsigned long)len + 1,
                &cmd[0], /* KEXEC_FILE_NO_INITRAMFS */ 0x4)) {
        si_log(si_error, "kexec: %m");
        close(fd);
        return;
    }
    close(fd);
    si_reboot(RB_KEXEC);
#else
    close(fd);
    si_log(si_error, "Found %s, but kexec is not supported...", kernel);
#endif
}

int
main(int argc, char *argv[])
{
    if (getpid() != 1)
        return 1;

    si_init_fs();
    si_init_fd("/dev/null", 0);
    si_init_fd("/dev/console", 1);
    si_init_fd("/dev/kmsg", 2);
    si_init_term();

    for (int i = 1; i < argc; i++)
        si_clear_str(argv[i]);

    sigset_t set, old;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, &old);

    while (1) {
        si_spawn((char*[]){"/etc/boot",0}, &old);
        si_spawn((char*[]){"/etc/reboot",0}, &old);
        si_update("/kernel");
        if (!access("/reboot", F_OK))
            si_reboot(RB_AUTOBOOT);
        sleep(1);
    }
}
