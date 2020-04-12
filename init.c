#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <asm-generic/setup.h>
#include <linux/kexec.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

static void
error(const char *fmt, ...)
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

    int fd = open("/dev/kmsg", O_WRONLY);

    if (fd < 0) {
        write(2, buf, size);
    } else {
        write(fd, buf, size);
        close(fd);
    }
}

static void
spawn(const char *cmd)
{
    pid_t pid = fork();

    if (pid == (pid_t)-1) {
        error("fork: %m\n");
        return;
    }

    if (pid == (pid_t)0) {
        sigset_t set;
        sigfillset(&set);
        sigprocmask(SIG_UNBLOCK, &set, NULL);

        setsid();
        execl(cmd, cmd, (char *)NULL);

        if (errno != ENOENT)
            error("execl(%s): %m\n", cmd);

        _exit(1);
    }

    while (1) {
        int status;
        pid_t ret = waitpid(-1, &status, 0);

        if (ret == (pid_t)-1) {
            if (errno == EINTR)
                continue;
            error("waitpid: %m\n");
            return;
        }

        if (ret == pid) {
            if (WIFEXITED(status) || WIFSIGNALED(status))
                return;
        }
    }
}

static void
mount_error(const char *src, const char *dst, const char *type,
          unsigned long flags, const void *data)
{
    mkdir(dst, 0755);

    if (mount(src, dst, type, flags, data))
        error("mount(%s): %m\n", dst);
}

static void
init_fs(void)
{
    chdir("/");

#define MS_NOSE MS_NOSUID | MS_NOEXEC
    mount_error(NULL, "/", NULL, MS_NOSUID | MS_REMOUNT, NULL);
    mount_error("none", "/proc", "proc", MS_NOSE | MS_NODEV, NULL);
    mount_error("none", "/sys", "sysfs", MS_NOSE | MS_NODEV, NULL);
    mount_error("none", "/sys/fs/cgroup", "cgroup2", MS_NOSE | MS_NODEV, NULL);
    mount_error("none", "/dev", "devtmpfs", MS_NOSE | MS_STRICTATIME, NULL);
    mount_error("devpts", "/dev/pts", "devpts", MS_NOSE, "gid=5,mode=0620");
#undef MS_NOSE

    mkdir("/dev/shm", 01777);
    mkdir("/tmp", 01777);

    symlink("/proc/mounts", "/etc/mtab");
    symlink("/proc/self/fd", "/dev/fd");
    symlink("/proc/self/fd/0", "/dev/stdin");
    symlink("/proc/self/fd/1", "/dev/stdout");
    symlink("/proc/self/fd/2", "/dev/stderror");
}

static void
init_console(const char *console)
{
    int fd = open(console, O_RDWR | O_NONBLOCK | O_NOCTTY);

    if (fd < 0) {
        error("open(%s): %m\n", console);
        return;
    }

    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);

    if (fd > 2)
        close(fd);
}

static void
init_term(void)
{
    if (getenv("TERM"))
        return;

    putenv("TERM=linux");
}

static void
clear_str(char *str)
{
    if (str) while (*str)
        *str++ = 0;
}

static int
read_file(const char *file, char *buf, size_t size)
{
    int fd = open(file, O_RDONLY);

    if (fd < 0) {
        error("open(%s): %m\n", file);
        return 1;
    }

    ssize_t ret = read(fd, buf, size);
    int tmp_errno = errno;
    close(fd);
    errno = tmp_errno;

    if (ret < 0) {
        error("read(%s): %m\n", file);
        return 1;
    }

    return 0;
}

static void
update(char *kernel)
{
    char cmd[COMMAND_LINE_SIZE] = {0};

    if (read_file("/proc/cmdline", cmd, sizeof(cmd) - 1))
        return;

    int fd = open(kernel, O_RDONLY);

    if (fd < 0) {
        if (errno != ENOENT)
            error("open(%s): %m\n", kernel);
        return;
    }

    unsigned long flags = KEXEC_FILE_NO_INITRAMFS;

    if (syscall(SYS_kexec_file_load, fd, 0,
                (unsigned long)sizeof(cmd), &cmd[0], flags)) {
        error("kexec: %m\n");
        return;
    }

    if (reboot(RB_KEXEC)) {
        error("reboot: %m\n");
        return;
    }
}

int
main(int argc, char *argv[])
{
    if (getpid() != 1)
        return 1;

    setsid();

    init_fs();
    init_console("/dev/console");
    init_term();

    for (int i = 1; i < argc; i++)
        clear_str(argv[i]);

    sigset_t set;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, NULL);

    while (1) {
        spawn("/etc/boot");
        spawn("/etc/reboot");
        update("/kernel");
        sleep(1);
    }
}
