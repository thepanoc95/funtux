/* funroot.c - FunTux multiroot object model (libfunobject).
 * FunMount / FunRoot / FunStratum. kernel module if loaded, else chroot. */

#define _DEFAULT_SOURCE

#include "funroot.h"
#include "funobj.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../funroot/uapi/funroot.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

struct fun_mount_ivars {
    const char *source;
    const char *target;
    const char *fstype;
    unsigned long flags;
    const char *data;
};

struct fun_root_ivars {
    unsigned index;         /* 0 = host root */
    char *path;
};

struct fun_stratum_ivars {
    struct fun_root_ivars root;
    char *name;             /* bedrock stratum name, "" = "any" */
};

static const struct fun_mount_ivars vfs_mounts[] = {
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

const char *fun_root_base(void)
{
    const char *env = getenv("MROOT_ROOTS_DIR");
    return (env && *env) ? env : "/var/roots";
}

static int ensure_dir(const char *path, mode_t mode)
{
    if (mkdir(path, mode) == 0)
        return 1;
    if (errno == EEXIST)
        return 1;
    if (errno == ENOENT) {
        const char *slash = strrchr(path, '/');
        if (slash && slash != path) {
            char *parent = strndup(path, (size_t)(slash - path));
            int ok = parent && ensure_dir(parent, mode);
            free(parent);
            if (!ok)
                return 0;
        }
        if (mkdir(path, mode) == 0)
            return 1;
        if (errno == EEXIST)
            return 1;
    }
    fprintf(stderr, "funroot: cannot create directory %s: %s\n",
            path, strerror(errno));
    return 0;
}

static char *join_root(const char *base, const char *p)
{
    if (!strcmp(base, "/"))
        return strdup(p);
    size_t n = strlen(base) + strlen(p) + 2;
    char *out = malloc(n);
    if (out)
        snprintf(out, n, "%s%s%s", base, p[0] == '/' ? "" : "/", p);
    return out;
}

static int is_mounted(const char *path)
{
    FILE *f = fopen("/proc/self/mountinfo", "r");
    if (!f)
        return 0;
    char line[4096];
    char needle[4200];
    snprintf(needle, sizeof needle, " %s ", path);
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, needle)) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

static int fk_available(void)
{
    int fd = open("/dev/funroot", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

static int fk_add(unsigned index, const char *path)
{
    int fd = open("/dev/funroot", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct funroot_req req;
    memset(&req, 0, sizeof req);
    req.index = index;
    strncpy(req.path, path, sizeof(req.path) - 1);
    int rc = ioctl(fd, FUNROOT_ADD, &req);
    if (rc != 0 && errno == EBUSY)
        rc = 0;
    close(fd);
    return rc;
}

static int fk_set(unsigned index)
{
    int fd = open("/dev/funroot", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct funroot_req req;
    memset(&req, 0, sizeof req);
    req.index = index;
    int rc = ioctl(fd, FUNROOT_SET, &req);
    close(fd);
    return rc;
}

static const char *strata_dir(void)
{
    return "/bedrock/strata";
}

static int bedrock_present(void)
{
    struct stat st;
    return stat(strata_dir(), &st) == 0 && S_ISDIR(st.st_mode);
}

static char **strata_names(size_t *count)
{
    *count = 0;
    DIR *d = opendir(strata_dir());
    if (!d)
        return NULL;
    size_t cap = 16, n = 0;
    char **names = calloc(cap, sizeof(char *));
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char full[4096];
        snprintf(full, sizeof full, "%s/%s", strata_dir(), e->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        if (n == cap) {
            cap *= 2;
            char **nn = realloc(names, cap * sizeof(char *));
            if (!nn)
                break;
            names = nn;
        }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    *count = n;
    return names;
}

static unsigned lowest_free_root_index(void)
{
    unsigned i;
    for (i = 1; i <= FUNROOT_MAX_INDEX; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "%u", i);
        char *p = join_root(fun_root_base(), buf);
        struct stat st;
        int used = p ? lstat(p, &st) == 0 : 1;
        free(p);
        if (!used)
            return i;
    }
    return 0;
}

static int index_for_stratum(const char *name, unsigned *out)
{
    char target[4096];
    snprintf(target, sizeof target, "%s/%s", strata_dir(), name);
    unsigned i;
    for (i = 1; i <= FUNROOT_MAX_INDEX; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "%u", i);
        char *p = join_root(fun_root_base(), buf);
        char link[4096];
        ssize_t n = p ? readlink(p, link, sizeof link - 1) : -1;
        free(p);
        if (n > 0) {
            link[n] = '\0';
            if (strcmp(target, link) == 0) {
                *out = i;
                return 1;
            }
        }
    }
    return 0;
}

static FunValue fm_init(FunObject *self, FunArgs args)
{
    (void)self;
    (void)args;
    return NULL;
}

static const char *mount_base(FunArgs args)
{
    return (args.count > 0 && args.args[0]) ? (const char *)args.args[0] : "/";
}

static FunValue fm_mount(FunObject *self, FunArgs args)
{
    struct fun_mount_ivars *iv = fun_ivars(self);
    char *target = join_root(mount_base(args), iv->target);
    if (!target)
        return NULL;
    if (!is_mounted(target)) {
        if (!ensure_dir(target, 0755)) {
            free(target);
            return NULL;
        }
        if (mount(iv->source, target, iv->fstype, iv->flags, iv->data) != 0 &&
            errno != EBUSY) {
            fprintf(stderr, "funroot: mount %s on %s: %s\n",
                    iv->fstype, target, strerror(errno));
            free(target);
            return NULL;
        }
    }
    free(target);
    return NULL;
}

static FunValue fm_umount(FunObject *self, FunArgs args)
{
    struct fun_mount_ivars *iv = fun_ivars(self);
    char *target = join_root(mount_base(args), iv->target);
    if (target) {
        if (umount2(target, MNT_DETACH) != 0) {
            if (errno != EINVAL && errno != ENOENT) {
                fprintf(stderr, "funroot: umount %s: %s\n",
                        target, strerror(errno));
            }
        }
        free(target);
    }
    return NULL;
}

static FunValue fm_is_mounted(FunObject *self, FunArgs args)
{
    struct fun_mount_ivars *iv = fun_ivars(self);
    char *target = join_root(mount_base(args), iv->target);
    int rc = target ? is_mounted(target) : 0;
    free(target);
    return (FunValue)(intptr_t)rc;
}

static FunValue fm_describe(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_mount_ivars *iv = fun_ivars(self);
    printf("%s: %s on %s (%s)\n", fun_class_name(self),
           iv->source, iv->target, iv->fstype);
    return NULL;
}

static const FunMethod fun_mount_methods[] = {
    { "init", fm_init },
    { "mount", fm_mount },
    { "umount", fm_umount },
    { "is-mounted", fm_is_mounted },
    { "describe", fm_describe },
};

static FunValue fr_init(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_root_ivars *iv = fun_ivars(self);
    if (iv->index == 0) {
        iv->path = strdup("/");
    } else {
        char buf[32];
        snprintf(buf, sizeof buf, "%u", iv->index);
        iv->path = join_root(fun_root_base(), buf);
    }
    return NULL;
}

static FunValue fr_describe(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_root_ivars *iv = fun_ivars(self);
    printf("%s: index %u @ %s\n", fun_class_name(self), iv->index,
           iv->path ? iv->path : "(unset)");
    return NULL;
}

static FunValue fr_mount(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_root_ivars *iv = fun_ivars(self);
    if (iv->index == 0) {
        printf("funroot: index 0 is the host root, nothing to mount\n");
        return NULL;
    }
    if (!iv->path)
        return NULL;
    size_t i;
    for (i = 0; i < ARRAY_LEN(vfs_mounts); i++) {
        FunObject *m = fun_mount_new(vfs_mounts[i].source,
                                     vfs_mounts[i].target,
                                     vfs_mounts[i].fstype,
                                     vfs_mounts[i].flags,
                                     vfs_mounts[i].data);
        fun_send1(m, "mount", (FunValue)iv->path);
        free(m);
    }
    return NULL;
}

static FunValue fr_umount(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_root_ivars *iv = fun_ivars(self);
    if (iv->index == 0) {
        printf("funroot: index 0 is the host root, nothing to unmount\n");
        return NULL;
    }
    if (!iv->path)
        return NULL;
    size_t i;
    for (i = 0; i < ARRAY_LEN(umount_targets); i++) {
        char *t = join_root(iv->path, umount_targets[i]);
        if (t) {
            if (umount2(t, MNT_DETACH) != 0) {
                if (errno != EINVAL && errno != ENOENT) {
                    fprintf(stderr, "funroot: umount %s: %s\n",
                            t, strerror(errno));
                }
            }
            free(t);
        }
    }
    return NULL;
}

static FunValue fr_register(FunObject *self, FunArgs args)
{
    struct fun_root_ivars *iv = fun_ivars(self);
    const char *path = (args.count > 0 && args.args[0])
        ? (const char *)args.args[0] : iv->path;
    if (!path)
        return NULL;
    if (fk_add(iv->index, path) != 0) {
        fprintf(stderr, "funroot: cannot register root %u at %s\n",
                iv->index, path);
        return NULL;
    }
    printf("funroot: root %u -> %s registered\n", iv->index, path);
    return NULL;
}

static FunValue fr_switch(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_root_ivars *iv = fun_ivars(self);
    if (fk_set(iv->index) != 0) {
        fprintf(stderr, "funroot: switch to root %u failed\n", iv->index);
        return (FunValue)(intptr_t)1;
    }
    if (chdir("/") != 0) {
        fprintf(stderr, "funroot: chdir(/): %s\n", strerror(errno));
        return (FunValue)(intptr_t)1;
    }
    printf("funroot: switched this process to root %u (%s)\n",
           iv->index, iv->path);
    return NULL;
}

static FunValue fr_enter(FunObject *self, FunArgs args)
{
    struct fun_root_ivars *iv = fun_ivars(self);
    const char *cmd = (args.count > 0 && args.args[0])
        ? (const char *)args.args[0] : "/bin/sh";
    if (!iv->path)
        return (FunValue)(intptr_t)1;

    char *argv[4];
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = (char *)cmd;
    argv[3] = NULL;

    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);

    if (fk_available()) {
        if (fk_add(iv->index, iv->path) != 0) {
            fprintf(stderr, "funroot: cannot register root %u\n", iv->index);
            return (FunValue)(intptr_t)1;
        }
        if (fk_set(iv->index) != 0) {
            fprintf(stderr, "funroot: switch to root %u failed\n", iv->index);
            return (FunValue)(intptr_t)1;
        }
    } else if (chroot(iv->path) != 0) {
        fprintf(stderr, "funroot: chroot(%s): %s\n", iv->path, strerror(errno));
        return (FunValue)(intptr_t)1;
    }

    if (chdir("/") != 0) {
        fprintf(stderr, "funroot: chdir(/): %s\n", strerror(errno));
        return (FunValue)(intptr_t)1;
    }
    execvp(argv[0], argv);
    fprintf(stderr, "funroot: exec: %s\n", strerror(errno));
    return (FunValue)(intptr_t)127;
}

static FunValue fr_status(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_root_ivars *iv = fun_ivars(self);
    printf("index: %u\n", iv->index);
    printf("path:  %s\n", iv->path ? iv->path : "(unset)");
    if (!iv->path)
        return NULL;
    struct stat st;
    if (stat(iv->path, &st) != 0) {
        printf("state: missing\n");
        return NULL;
    }
    printf("state: present\n");
    if (iv->index != 0) {
        printf("mounts:\n");
        size_t i;
        for (i = 0; i < ARRAY_LEN(vfs_mounts); i++) {
            char *t = join_root(iv->path, vfs_mounts[i].target);
            if (t) {
                printf("  %s %s\n", is_mounted(t) ? "[x]" : "[ ]", t);
                free(t);
            }
        }
    }
    return NULL;
}

static FunValue fr_list(FunObject *self, FunArgs args)
{
    (void)self;
    (void)args;
    printf("index\tpath\n");
    printf("0\t/\n");
    unsigned i;
    for (i = 1; i <= FUNROOT_MAX_INDEX; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "%u", i);
        char *p = join_root(fun_root_base(), buf);
        if (!p)
            continue;
        struct stat st;
        if (lstat(p, &st) == 0)
            printf("%u\t%s\n", i, p);
        free(p);
    }
    return NULL;
}

static FunValue fr_strata(FunObject *self, FunArgs args)
{
    (void)self;
    (void)args;
    if (!bedrock_present()) {
        printf("funroot: no Bedrock strata (%s not present)\n", strata_dir());
        return NULL;
    }
    size_t n = 0;
    char **names = strata_names(&n);
    if (!names)
        return NULL;
    size_t i;
    for (i = 0; i < n; i++)
        printf("%s\n", names[i]);
    for (i = 0; i < n; i++)
        free(names[i]);
    free(names);
    return NULL;
}

static FunValue fr_bedrock_sync(FunObject *self, FunArgs args)
{
    (void)self;
    (void)args;
    if (!bedrock_present()) {
        printf("funroot: no Bedrock strata (%s not present)\n", strata_dir());
        return NULL;
    }
    if (!ensure_dir(fun_root_base(), 0755))
        return NULL;
    size_t n = 0;
    char **names = strata_names(&n);
    if (!names)
        return NULL;
    size_t i;
    unsigned added = 0;
    for (i = 0; i < n; i++) {
        unsigned idx;
        if (index_for_stratum(names[i], &idx)) {
            printf("funroot: stratum %s already mapped to root %u\n",
                   names[i], idx);
            continue;
        }
        idx = lowest_free_root_index();
        if (idx == 0) {
            fprintf(stderr, "funroot: no free root index for stratum %s\n",
                    names[i]);
            continue;
        }
        char buf[32], target[4096], link[4096];
        snprintf(buf, sizeof buf, "%u", idx);
        snprintf(target, sizeof target, "%s/%s", strata_dir(), names[i]);
        snprintf(link, sizeof link, "%s/%s", fun_root_base(), buf);
        if (symlink(target, link) != 0 && errno != EEXIST) {
            fprintf(stderr, "funroot: cannot link %s -> %s: %s\n",
                    link, target, strerror(errno));
            continue;
        }
        printf("funroot: mapped stratum %s -> root %u (%s)\n",
               names[i], idx, target);
        added++;
    }
    printf("funroot: bedrock sync complete (%u new mapping(s), %zu stratum/strata total)\n",
           added, n);
    for (i = 0; i < n; i++)
        free(names[i]);
    free(names);
    return NULL;
}

static const FunMethod fun_root_methods[] = {
    { "init", fr_init },
    { "describe", fr_describe },
    { "mount", fr_mount },
    { "umount", fr_umount },
    { "register", fr_register },
    { "switch", fr_switch },
    { "enter", fr_enter },
    { "status", fr_status },
    { "list", fr_list },
    { "strata", fr_strata },
    { "bedrock-sync", fr_bedrock_sync },
};

static FunValue fs_init(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_stratum_ivars *iv = fun_ivars(self);
    if (iv->name && *iv->name) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s/%s", strata_dir(), iv->name);
        iv->root.path = strdup(buf);
    } else {
        iv->root.path = strdup(strata_dir());
    }
    return NULL;
}

static FunValue fs_name(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_stratum_ivars *iv = fun_ivars(self);
    return (FunValue)iv->name;
}

static FunValue fs_describe(FunObject *self, FunArgs args)
{
    (void)args;
    struct fun_stratum_ivars *iv = fun_ivars(self);
    if (iv->name && *iv->name)
        printf("%s: %s @ %s\n", fun_class_name(self),
               iv->name, iv->root.path);
    else
        printf("%s: %s\n", fun_class_name(self), iv->root.path);
    return NULL;
}

static const FunMethod fun_stratum_methods[] = {
    { "init", fs_init },
    { "name", fs_name },
    { "describe", fs_describe },
};

static struct {
    FunClass *mount;
    FunClass *root;
    FunClass *stratum;
} funroot_classes_;

static void ensure_classes(void)
{
    if (funroot_classes_.mount)
        return;
    funroot_classes_.mount = fun_class_create(
        "FunMount", NULL, fun_mount_methods,
        (unsigned)ARRAY_LEN(fun_mount_methods),
        sizeof(struct fun_mount_ivars));
    funroot_classes_.root = fun_class_create(
        "FunRoot", NULL, fun_root_methods,
        (unsigned)ARRAY_LEN(fun_root_methods),
        sizeof(struct fun_root_ivars));
    funroot_classes_.stratum = fun_class_create(
        "FunStratum", funroot_classes_.root, fun_stratum_methods,
        (unsigned)ARRAY_LEN(fun_stratum_methods),
        sizeof(struct fun_stratum_ivars));
}

FunObject *fun_mount_new(const char *source, const char *target,
                         const char *fstype, unsigned long flags,
                         const char *data)
{
    ensure_classes();
    FunObject *obj = fun_alloc(funroot_classes_.mount);
    if (!obj)
        return NULL;
    struct fun_mount_ivars *iv = fun_ivars(obj);
    iv->source = source;
    iv->target = target;
    iv->fstype = fstype;
    iv->flags = flags;
    iv->data = data;
    return obj;
}

FunObject *fun_root_new(unsigned index)
{
    ensure_classes();
    FunObject *obj = fun_alloc(funroot_classes_.root);
    if (!obj)
        return NULL;
    struct fun_root_ivars *iv = fun_ivars(obj);
    iv->index = index;
    fun_send0(obj, "init");
    return obj;
}

FunObject *fun_root_new_path(unsigned index, const char *path)
{
    ensure_classes();
    FunObject *obj = fun_alloc(funroot_classes_.root);
    if (!obj)
        return NULL;
    struct fun_root_ivars *iv = fun_ivars(obj);
    iv->index = index;
    iv->path = strdup(path);
    return obj;
}

FunObject *fun_stratum_new(const char *name)
{
    ensure_classes();
    FunObject *obj = fun_alloc(funroot_classes_.stratum);
    if (!obj)
        return NULL;
    struct fun_stratum_ivars *iv = fun_ivars(obj);
    iv->name = name ? strdup(name) : strdup("");
    fun_send0(obj, "init");
    return obj;
}
