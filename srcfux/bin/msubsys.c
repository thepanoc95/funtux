#define _DEFAULT_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MSUBSYS_VERSION "0.1.0"
#define MSUBSYS_DIR_DEFAULT "/var/msubsys"

struct MountSpec {
    const char *source;
    const char *target;
    const char *fstype;
    unsigned long flags;
    const char *data;
};

static const struct MountSpec vfs_mounts[] = {
    { "proc",     "/proc",     "proc",     MS_NOSUID | MS_NOEXEC | MS_NODEV, "" },
    { "sysfs",    "/sys",      "sysfs",    MS_NOSUID | MS_NOEXEC | MS_NODEV, "" },
    { "devtmpfs", "/dev",      "devtmpfs", MS_NOSUID,                         "mode=0755" },
    { "devpts",   "/dev/pts",  "devpts",   MS_NOSUID | MS_NOEXEC,             "newinstance,ptmxmode=0666,mode=0620,gid=5" },
    { "tmpfs",    "/dev/shm",  "tmpfs",    MS_NOSUID | MS_NODEV,              "mode=1777" },
    { "tmpfs",    "/tmp",      "tmpfs",    MS_NOSUID | MS_NODEV,              "mode=1777" },
    { "tmpfs",    "/run",      "tmpfs",    MS_NOSUID | MS_NODEV,              "mode=0755" },
};

static const char *umount_targets[] = {
    "/etc/resolv.conf", "/run", "/tmp", "/dev/shm", "/dev/pts", "/dev", "/sys", "/proc",
};

static const char *skeleton_dirs[] = {
    "bin", "sbin", "lib", "lib64",
    "usr", "usr/bin", "usr/sbin", "usr/lib", "usr/lib64",
    "usr/local", "usr/local/bin", "usr/local/sbin", "usr/local/lib", "usr/local/etc",
    "etc", "opt", "srv", "mnt",
    "dev", "dev/pts", "dev/shm",
    "proc", "sys", "run",
    "tmp",
    "var", "var/cache", "var/lock", "var/log", "var/run", "var/tmp",
    "root", "home",
};

static const char *subsystems_dir(void) {
    const char *env = getenv("MSUBSYS_DIR");
    return env && *env ? env : MSUBSYS_DIR_DEFAULT;
}

static int require_root(void) {
    if (geteuid() != 0) {
        fprintf(stderr, "msubsys: this operation requires root privileges\n");
        return 0;
    }
    return 1;
}

static int valid_name(const char *name) {
    if (!name || !*name) return 0;
    if (strchr(name, '/')) return 0;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 0;
    if (name[0] == '.') return 0;
    return 1;
}

static char *ss_path(const char *name) {
    if (!valid_name(name)) {
        fprintf(stderr, "msubsys: invalid sub-system name `%s`\n", name);
        return NULL;
    }
    size_t len = strlen(subsystems_dir()) + 1 + strlen(name) + 1;
    char *p = malloc(len);
    if (p) snprintf(p, len, "%s/%s", subsystems_dir(), name);
    return p;
}

static int ensure_dir(const char *path, mode_t mode) {
    if (mkdir(path, mode) == 0) return 1;
    if (errno == EEXIST) return 1;
    if (errno == ENOENT) {
        const char *slash = strrchr(path, '/');
        if (slash && slash != path) {
            char *parent = strndup(path, (size_t)(slash - path));
            int ok = parent && ensure_dir(parent, 0755);
            free(parent);
            if (!ok) return 0;
        }
        if (mkdir(path, mode) == 0) return 1;
        if (errno == EEXIST) return 1;
    }
    fprintf(stderr, "msubsys: cannot create directory %s: %s\n",
            path, strerror(errno));
    return 0;
}

static void make_symlink(const char *target, const char *link) {
    if (symlink(target, link) != 0 && errno != EEXIST) {
        fprintf(stderr, "msubsys: symlink %s -> %s: %s\n",
                link, target, strerror(errno));
    }
}

static int is_mounted(const char *path) {
    FILE *f = fopen("/proc/self/mountinfo", "r");
    if (!f) return 0;
    char line[4096];
    char needle[4096];
    snprintf(needle, sizeof(needle), " %s ", path);
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

static char *join(const char *root, const char *path) {
    if (!strcmp(root, "/")) return strdup(path);
    int sep = path[0] == '/' ? 0 : 1;
    size_t len = strlen(root) + strlen(path) + (size_t)sep + 1;
    char *p = malloc(len);
    if (p) snprintf(p, len, "%s%s%s", root, sep ? "/" : "", path);
    return p;
}

static char *sq(const char *s) {
    size_t len = strlen(s) * 2 + 3;
    char *out = malloc(len);
    if (!out) return NULL;
    char *d = out;
    *d++ = '\'';
    for (const char *c = s; *c; c++) {
        if (*c == '\'') {
            *d++ = '\'';
            *d++ = '\\';
            *d++ = '\'';
            *d++ = '\'';
        } else {
            *d++ = *c;
        }
    }
    *d++ = '\'';
    *d = '\0';
    return out;
}

static int rmtree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) return 1;
        fprintf(stderr, "msubsys: lstat %s: %s\n", path, strerror(errno));
        return 0;
    }
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "msubsys: opendir %s: %s\n", path, strerror(errno));
            return 0;
        }
        struct dirent *e;
        int ok = 1;
        while ((e = readdir(d))) {
            if (!ok) break;
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char *p = join(path, "/");
            char *c = join(p, e->d_name);
            free(p);
            if (!c) { ok = 0; break; }
            if (!rmtree(c)) ok = 0;
            free(c);
        }
        closedir(d);
        if (ok && rmdir(path) != 0) {
            fprintf(stderr, "msubsys: rmdir %s: %s\n", path, strerror(errno));
            ok = 0;
        }
        return ok;
    }
    if (unlink(path) != 0) {
        fprintf(stderr, "msubsys: unlink %s: %s\n", path, strerror(errno));
        return 0;
    }
    return 1;
}

static int mount_all(const char *rp) {
    for (size_t i = 0; i < sizeof(vfs_mounts) / sizeof(vfs_mounts[0]); i++) {
        const struct MountSpec *m = &vfs_mounts[i];
        char *target = join(rp, m->target);
        if (!target) return 0;
        if (is_mounted(target)) {
            free(target);
            continue;
        }
        if (ensure_dir(target, 0755) &&
            mount(m->source, target, m->fstype, m->flags, m->data) != 0 &&
            errno != EBUSY) {
            fprintf(stderr, "msubsys: mount %s on %s: %s\n",
                    m->fstype, target, strerror(errno));
        }
        free(target);
    }
    return 1;
}

static int umount_all(const char *rp) {
    for (size_t i = 0; i < sizeof(umount_targets) / sizeof(umount_targets[0]); i++) {
        char *t = join(rp, umount_targets[i]);
        if (!t) return 0;
        if (umount2(t, MNT_DETACH) != 0) {
            if (errno != EINVAL && errno != ENOENT) {
                fprintf(stderr, "msubsys: umount %s: %s\n", t, strerror(errno));
            }
        }
        free(t);
    }
    return 1;
}

static int mount_dns(const char *rp) {
    if (access("/etc/resolv.conf", F_OK) != 0) return 1;
    char *target = join(rp, "/etc/resolv.conf");
    if (!target) return 0;
    ensure_dir(join(rp, "/etc"), 0755);
    if (is_mounted(target)) {
        free(target);
        return 1;
    }
    if (access(target, F_OK) != 0) {
        int fd = open(target, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) close(fd);
    }
    if (mount("/etc/resolv.conf", target, NULL, MS_BIND, NULL) != 0) {
        fprintf(stderr, "msubsys: bind mount %s: %s\n", target, strerror(errno));
        free(target);
        return 0;
    }
    free(target);
    return 1;
}

static int cmd_create(const char *name, const char *copy_from) {
    char *rp = ss_path(name);
    if (!rp) return 1;
    if (access(rp, F_OK) == 0) {
        fprintf(stderr, "msubsys: sub-system %s already exists\n", name);
        free(rp);
        return 1;
    }
    if (!ensure_dir(rp, 0755)) {
        free(rp);
        return 1;
    }
    if (copy_from) {
        if (access(copy_from, F_OK) != 0) {
            fprintf(stderr, "msubsys: copy source %s not found\n", copy_from);
            free(rp);
            return 1;
        }
        char *src = sq(copy_from);
        char *dst = sq(rp);
        char *cmd = NULL;
        size_t n = snprintf(NULL, 0, "cp -a %s/. %s/", src, dst);
        cmd = malloc(n + 1);
        if (cmd) {
            snprintf(cmd, n + 1, "cp -a %s/. %s/", src, dst);
            if (system(cmd) != 0) {
                fprintf(stderr, "msubsys: could not copy %s into %s\n",
                        copy_from, rp);
                free(cmd);
                free(src);
                free(dst);
                free(rp);
                return 1;
            }
            free(cmd);
        }
        free(src);
        free(dst);
    }
    for (size_t i = 0; i < sizeof(skeleton_dirs) / sizeof(skeleton_dirs[0]); i++) {
        char *p = join(rp, skeleton_dirs[i]);
        if (p) {
            ensure_dir(p, 0755);
            free(p);
        }
    }
    char *tmp = join(rp, "/tmp");
    char *vtmp = join(rp, "/var/tmp");
    char *shm = join(rp, "/dev/shm");
    char *root = join(rp, "/root");
    if (tmp) { chmod(tmp, 01777); free(tmp); }
    if (vtmp) { chmod(vtmp, 01777); free(vtmp); }
    if (shm) { chmod(shm, 01777); free(shm); }
    if (root) { chmod(root, 0700); free(root); }
    make_symlink("/proc/self/fd", join(rp, "/dev/fd"));
    make_symlink("/proc/self/fd/0", join(rp, "/dev/stdin"));
    make_symlink("/proc/self/fd/1", join(rp, "/dev/stdout"));
    make_symlink("/proc/self/fd/2", join(rp, "/dev/stderr"));
    make_symlink("pts/ptmx", join(rp, "/dev/ptmx"));
    printf("msubsys: created sub-system %s at %s\n", name, rp);
    free(rp);
    return 0;
}

static int chroot_exec(const char *rp, const char *cmd, char *const *argv,
                       int new_session, int dns) {
    if (chroot(rp) != 0) {
        fprintf(stderr, "msubsys: chroot(%s): %s\n", rp, strerror(errno));
        return 1;
    }
    if (chdir("/") != 0) {
        fprintf(stderr, "msubsys: chdir(/): %s\n", strerror(errno));
        return 1;
    }
    char term[64] = {0};
    if (const char *t = getenv("TERM")) snprintf(term, sizeof(term), "%s", t);
    clearenv();
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("HOME", "/root", 1);
    setenv("TERM", term[0] ? term : "linux", 1);
    setenv("SHELL", cmd, 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    (void)dns;
    if (new_session) {
        if (getsid(0) != getpid()) {
            if (setsid() < 0 && errno == EPERM) {
                pid_t pid = fork();
                if (pid > 0) _exit(0);
                if (setsid() < 0) return 1;
            }
            if (isatty(0)) ioctl(0, TIOCSCTTY, 0);
        }
    }
    execvp(cmd, argv);
    fprintf(stderr, "msubsys: exec %s: %s\n", cmd, strerror(errno));
    return 127;
}

static int cmd_enter(const char *name, int argc, char **argv,
                     int new_session, int dns, int do_mount) {
    if (!require_root()) return 1;
    char *rp = ss_path(name);
    if (!rp) return 1;
    if (access(rp, F_OK) != 0) {
        fprintf(stderr, "msubsys: sub-system %s does not exist (run create first)\n", name);
        free(rp);
        return 1;
    }
    if (do_mount) {
        mount_all(rp);
        if (dns) mount_dns(rp);
    }
    const char *cmd = "/bin/sh";
    char **rest = NULL;
    if (argc > 0) {
        cmd = argv[0];
        rest = argv;
    } else {
        static char *default_argv[] = { NULL };
        default_argv[0] = "/bin/sh";
        rest = default_argv;
    }
    int rc = chroot_exec(rp, cmd, rest, new_session, dns);
    free(rp);
    return rc;
}

static int cmd_exec(const char *name, int argc, char **argv,
                    int dns, int do_mount) {
    if (!require_root()) return 1;
    if (argc == 0) {
        fprintf(stderr, "msubsys: exec requires a command\n");
        return 2;
    }
    char *rp = ss_path(name);
    if (!rp) return 1;
    if (access(rp, F_OK) != 0) {
        fprintf(stderr, "msubsys: sub-system %s does not exist\n", name);
        free(rp);
        return 1;
    }
    if (do_mount) {
        mount_all(rp);
        if (dns) mount_dns(rp);
    }
    int rc = chroot_exec(rp, argv[0], argv, 0, dns);
    free(rp);
    return rc;
}

static int cmd_list(void) {
    const char *dir = subsystems_dir();
    DIR *d = opendir(dir);
    if (!d) {
        printf("msubsys: no sub-systems (nothing under %s)\n", dir);
        return 0;
    }
    printf("name\tpath\tmounts\n");
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        size_t len = strlen(dir) + 1 + strlen(e->d_name) + 1;
        char *full = malloc(len);
        if (!full) continue;
        snprintf(full, len, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            size_t mcount = 0;
            for (size_t i = 0; i < sizeof(vfs_mounts) / sizeof(vfs_mounts[0]); i++) {
                char *t = join(full, vfs_mounts[i].target);
                if (t) {
                    if (is_mounted(t)) mcount++;
                    free(t);
                }
            }
            printf("%s\t%s\t%zu/%zu\n", e->d_name, full, mcount,
                   sizeof(vfs_mounts) / sizeof(vfs_mounts[0]));
        }
        free(full);
    }
    closedir(d);
    return 0;
}

static int cmd_status(const char *name) {
    char *rp = ss_path(name);
    if (!rp) return 1;
    printf("name:  %s\n", name);
    printf("path:  %s\n", rp);
    struct stat st;
    if (stat(rp, &st) != 0) {
        printf("state: missing\n");
        free(rp);
        return 1;
    }
    printf("state: present\n");
    for (size_t i = 0; i < sizeof(vfs_mounts) / sizeof(vfs_mounts[0]); i++) {
        char *t = join(rp, vfs_mounts[i].target);
        if (t) {
            printf("  %s %s\n", is_mounted(t) ? "[x]" : "[ ]", t);
            free(t);
        }
    }
    free(rp);
    return 0;
}

static int cmd_remove(const char *name, int force) {
    if (!require_root()) return 1;
    char *rp = ss_path(name);
    if (!rp) return 1;
    if (access(rp, F_OK) != 0) {
        fprintf(stderr, "msubsys: sub-system %s does not exist\n", name);
        free(rp);
        return 1;
    }
    if (!force) {
        printf("msubsys: delete sub-system %s at %s? [y/N] ", name, rp);
        fflush(stdout);
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) line[0] = '\0';
        if (line[0] != 'y' && line[0] != 'Y') {
            printf("msubsys: aborted\n");
            free(rp);
            return 1;
        }
    }
    umount_all(rp);
    if (!rmtree(rp)) {
        fprintf(stderr, "msubsys: failed to remove %s\n", rp);
        free(rp);
        return 1;
    }
    printf("msubsys: removed sub-system %s\n", name);
    free(rp);
    return 0;
}

static void print_usage(FILE *out) {
    fprintf(out,
        "msubsys %s - FunTux sub-system (chroot) manager\n\n"
        "Usage: msubsys [options] <command> [args...]\n\n"
        "Commands:\n"
        "  create <name> [--copy <dir>]  create a sub-system, optionally copying a base\n"
        "  enter <name> [cmd...]         chroot into a sub-system (default /bin/sh)\n"
        "  exec <name> [--mount] <cmd>   run a command inside a sub-system\n"
        "  mount <name>                  mount virtual filesystems (proc, sys, dev, ...)\n"
        "  umount <name>                 unmount the virtual filesystems\n"
        "  list                          list sub-systems\n"
        "  status <name>                 show a sub-system's state\n"
        "  remove <name>                 delete a sub-system\n\n"
        "Sub-systems live under %s (override with $MSUBSYS_DIR).\n\n"
        "Options:\n"
        "  -f, --force      do not ask for confirmation (remove)\n"
        "  -d, --dns        bind-mount the host's /etc/resolv.conf\n"
        "  -s, --session    create a new session (setsid) before entering\n"
        "  -m, --mount      mount virtual filesystems (exec)\n"
        "  -n, --no-mount   do not mount virtual filesystems (enter)\n"
        "  -c, --copy <dir> copy a base directory into the new sub-system (create)\n"
        "  -h, --help       show this help and exit\n"
        "  -V, --version    show version and exit\n",
        MSUBSYS_VERSION, MSUBSYS_DIR_DEFAULT);
}

int main(int argc, char **argv) {
    static const struct option long_options[] = {
        { "help",    no_argument,       NULL, 'h' },
        { "version", no_argument,       NULL, 'V' },
        { "force",   no_argument,       NULL, 'f' },
        { "dns",     no_argument,       NULL, 'd' },
        { "session", no_argument,       NULL, 's' },
        { "mount",   no_argument,       NULL, 'm' },
        { "no-mount", no_argument,      NULL, 'n' },
        { "copy",    required_argument, NULL, 'c' },
        { NULL, 0, NULL, 0 },
    };

    int force = 0;
    int dns = 0;
    int new_session = 0;
    int do_mount = 0;
    int no_mount = 0;
    const char *copy_from = NULL;

    int opt;
    while ((opt = getopt_long(argc, argv, "hVfdsmnc:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h': print_usage(stdout); return 0;
            case 'V': printf("msubsys %s\n", MSUBSYS_VERSION); return 0;
            case 'f': force = 1; break;
            case 'd': dns = 1; break;
            case 's': new_session = 1; break;
            case 'm': do_mount = 1; break;
            case 'n': no_mount = 1; break;
            case 'c': copy_from = optarg; break;
            default: print_usage(stderr); return 2;
        }
    }

    if (optind >= argc) {
        print_usage(stderr);
        return 2;
    }

    const char *command = argv[optind++];
    const char *name = NULL;
    if (optind < argc) name = argv[optind++];
    int rest_argc = argc - optind;
    char **rest_argv = &argv[optind];

    if (!strcmp(command, "list")) return cmd_list();
    if (!strcmp(command, "help")) {
        print_usage(stdout);
        return 0;
    }

    if (!name) {
        fprintf(stderr, "msubsys: %s requires a sub-system name\n", command);
        return 2;
    }

    if (!strcmp(command, "create"))
        return cmd_create(name, copy_from);
    if (!strcmp(command, "status"))
        return cmd_status(name);
    if (!strcmp(command, "mount")) {
        if (!require_root()) return 1;
        char *rp = ss_path(name);
        if (!rp) return 1;
        if (access(rp, F_OK) != 0) {
            fprintf(stderr, "msubsys: sub-system %s does not exist\n", name);
            free(rp);
            return 1;
        }
        int rc = mount_all(rp) ? 0 : 1;
        free(rp);
        return rc;
    }
    if (!strcmp(command, "umount") || !strcmp(command, "unmount")) {
        if (!require_root()) return 1;
        char *rp = ss_path(name);
        if (!rp) return 1;
        int rc = umount_all(rp) ? 0 : 1;
        free(rp);
        return rc;
    }
    if (!strcmp(command, "enter"))
        return cmd_enter(name, rest_argc, rest_argv, new_session, dns, !no_mount);
    if (!strcmp(command, "exec"))
        return cmd_exec(name, rest_argc, rest_argv, dns, do_mount);
    if (!strcmp(command, "remove"))
        return cmd_remove(name, force);

    fprintf(stderr, "msubsys: unknown command `%s`\n", command);
    print_usage(stderr);
    return 2;
}
