#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>

#include <asm-generic/setup.h>
#include <linux/kexec.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

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
    char buf[256];
    size_t size = sizeof(buf);

    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    if (ret <= 0)
        return;

    if (size > (size_t)ret)
        size = (size_t)ret;

    char hdr[] = "<0>/init: ";
    struct iovec iov[] = {
        {hdr, sizeof(hdr) - 1},
        {buf, size}, {"\n", 1},
    };

    hdr[1] = '0' + (lvl & 7);
    writev(2, iov, 3);
}

static void
si_spawn(const char *cmd)
{
    si_log(si_info, "Running %s", cmd);

    pid_t pid = fork();

    if (pid == (pid_t)-1) {
        si_log(si_critical, "fork: %m");
        return;
    }

    if (pid == (pid_t)0) {
        sigset_t set;
        sigfillset(&set);
        sigprocmask(SIG_UNBLOCK, &set, NULL);

        setsid();
        execl(cmd, cmd, (char *)NULL);

        if (errno != ENOENT)
            si_log(si_critical, "execl(%s): %m", cmd);

        _exit(1);
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
    chdir("/");

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

    symlink("/proc/mounts", "/etc/mtab");
    symlink("/proc/self/fd", "/dev/fd");
    symlink("/proc/self/fd/0", "/dev/stdin");
    symlink("/proc/self/fd/1", "/dev/stdout");
    symlink("/proc/self/fd/2", "/dev/stderr");
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

static int
si_read_file(const char *file, char *buf, size_t size)
{
    int fd = open(file, O_RDONLY);

    if (fd < 0) {
        si_log(si_error, "open(%s): %m", file);
        return 1;
    }

    ssize_t ret = read(fd, buf, size);
    int tmp_errno = errno;
    close(fd);
    errno = tmp_errno;

    if (ret < 0) {
        si_log(si_error, "read(%s): %m", file);
        return 1;
    }

    return 0;
}

static void
si_update(char *kernel)
{
    char cmd[COMMAND_LINE_SIZE] = {0};

    if (si_read_file("/proc/cmdline", cmd, sizeof(cmd) - 1))
        return;

    int fd = open(kernel, O_RDONLY);

    if (fd < 0) {
        if (errno != ENOENT)
            si_log(si_error, "open(%s): %m", kernel);
        return;
    }

    si_log(si_info, "Found %s, loading...", kernel);

    unsigned long flags = KEXEC_FILE_NO_INITRAMFS;

    if (syscall(SYS_kexec_file_load, fd, 0,
                (unsigned long)sizeof(cmd), &cmd[0], flags)) {
        si_log(si_error, "kexec: %m");
        return;
    }

    si_log(si_info, "Rebooting...");

    if (reboot(RB_KEXEC)) {
        si_log(si_error, "reboot: %m");
        return;
    }
}

int
main(int argc, char *argv[])
{
    if (getpid() != 1)
        return 1;

    setsid();

    si_init_fs();
    si_init_fd("/dev/null", 0);
    si_init_fd("/dev/console", 1);
    si_init_fd("/dev/kmsg", 2);
    si_init_term();

    for (int i = 1; i < argc; i++)
        si_clear_str(argv[i]);

    sigset_t set;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, NULL);

    while (1) {
        si_spawn("/etc/boot");
        si_spawn("/etc/reboot");
        si_update("/kernel");
        sleep(1);
    }
}
