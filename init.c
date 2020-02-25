#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

static void
spawn(const char *cmd)
{
    pid_t pid = fork();

    if (pid == (pid_t)-1) {
        perror("fork");
        return;
    }

    if (pid == (pid_t)0) {
        sigset_t set;
        sigfillset(&set);
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        setsid();
        execl(cmd, cmd, (char *)NULL);
        perror("execl");
        _exit(1);
    }

    while (1) {
        int status;
        pid_t ret = waitpid(-1, &status, 0);

        if (ret == (pid_t)-1) {
            if (errno == EINTR)
                continue;
            perror("waitpid");
            return;
        }

        if (ret == pid) {
            if (WIFEXITED(status) || WIFSIGNALED(status))
                return;
        }
    }
}

static void
init_fs(void)
{
    chdir("/");

    if (mount(NULL, "/", NULL, MS_NOSUID | MS_REMOUNT, NULL))
        perror("remount(/)");

    mkdir("/dev", 0755);
    mkdir("/proc", 0755);
    mkdir("/tmp", 01777);
    mkdir("/sys", 0755);

    if (mount("none", "/dev", "devtmpfs", 0, NULL))
        perror("mount(dev)");

    if (mount("none", "/proc", "proc", 0, NULL))
        perror("mount(proc)");

    if (mount("none", "/sys", "sysfs", 0, NULL))
        perror("mount(sys)");

    if (mount("none", "/sys/fs/cgroup", "cgroup2", 0, NULL))
        perror("mount(cgroup2)");

    mkdir("/dev/pts", 0755);
    mkdir("/dev/shm", 0755);

    if (mount("devpts", "/dev/pts", "devpts", 0, "gid=5,mode=0620,ptmxmode=0666"))
        perror("mount(devpts)");

    symlink("/proc/mounts", "/etc/mtab");
    symlink("/proc/self/fd", "/dev/fd");
    symlink("/proc/self/fd/0", "/dev/stdin");
    symlink("/proc/self/fd/1", "/dev/stdout");
    symlink("/proc/self/fd/2", "/dev/stderr");
}

static void
init_console(void)
{
    int fd = open("/dev/console", O_RDWR | O_NONBLOCK | O_NOCTTY);

    if (fd < 0) {
        perror("open(console)");
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
    if (str) while (*str) *str++ = 0;
}

int
main(int argc, char *argv[])
{
    if (getpid() != 1)
        return 1;

    setsid();

    init_fs();
    init_console();
    init_term();

    for (int i = 1; i < argc; i++)
        clear_str(argv[i]);

    sigset_t set;
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, NULL);

    while (1) {
        spawn("/etc/boot");
        spawn("/etc/reboot");
        sleep(1);
    }

    return 0;
}
