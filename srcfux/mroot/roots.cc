#include "roots.h"
#include "kernel.h"
#include "media.h"
#include "utils.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(__linux__) || defined(__LINUX__)
    #include <sys/mount.h>
#endif
#include <unistd.h>

namespace mroot {

const MountSpec vfs_mounts[] = {
    { "proc",     "/proc",    "proc",     MS_NOSUID | MS_NOEXEC | MS_NODEV, "" },
    { "sysfs",    "/sys",     "sysfs",    MS_NOSUID | MS_NOEXEC | MS_NODEV, "" },
    { "devtmpfs", "/dev",     "devtmpfs", MS_NOSUID,                         "mode=0755" },
    { "devpts",   "/dev/pts", "devpts",   MS_NOSUID | MS_NOEXEC,             "newinstance,ptmxmode=0666,mode=0620,gid=5" },
    { "tmpfs",    "/dev/shm", "tmpfs",    MS_NOSUID | MS_NODEV,              "mode=1777" },
    { "tmpfs",    "/tmp",     "tmpfs",    MS_NOSUID | MS_NODEV,              "mode=1777" },
    { "tmpfs",    "/run",     "tmpfs",    MS_NOSUID | MS_NODEV,              "mode=0755" },
};

const char *umount_targets[] = {
    "/etc/resolv.conf", "/run", "/tmp", "/dev/shm", "/dev/pts", "/dev", "/sys", "/proc",
};

const char *skeleton_dirs[] = {
    "bin", "sbin", "lib", "lib64",
    "usr", "usr/bin", "usr/sbin", "usr/lib", "usr/lib64",
    "usr/local", "usr/local/bin", "usr/local/sbin", "usr/local/lib", "usr/local/etc",
    "etc", "opt", "srv", "mnt", "media",
    "dev", "dev/pts", "dev/shm",
    "proc", "sys", "run",
    "tmp",
    "var", "var/cache", "var/lock", "var/log", "var/run", "var/spool", "var/tmp",
    "var/roots",
    "root", "home",
};

const char *skip_names[] = { "proc", "sys", "dev", "run", "tmp" };

MultiRoot::MultiRoot(unsigned index)
    : index_(index),
      path_(index == 0 ? "/" : roots_dir() + "/" + std::to_string(index)) {
    load_media_conf(index_, media_);
}

unsigned MultiRoot::index() const { return index_; }
const std::string &MultiRoot::path() const { return path_; }
bool MultiRoot::is_host() const { return index_ == 0; }
bool MultiRoot::is_media() const { return media_.valid; }
bool MultiRoot::is_stratum() const {
    std::string rp = is_media() ? status_path() : path_;
    return rp.compare(0, sizeof("/bedrock/strata/") - 1, "/bedrock/strata/") == 0;
}
MediaConf &MultiRoot::media() { return media_; }

std::string MultiRoot::status_path() const {
    if (!media_.valid) return path_;
    return media_.mountpoint + "/" + media_.relpath;
}

bool MultiRoot::init() {
    if (is_host()) {
        std::cout << "mroot: index 0 is the host root, nothing to initialize\n";
        return true;
    }
    if (!require_root()) return false;
    std::string rp = work_path(*this);
    if (rp.empty()) return false;
    if (is_stratum()) {
        std::cout << "mroot: root " << index_
                  << " is a Bedrock stratum; skeleton not needed\n";
        return true;
    }
    struct stat st;
    bool existed = (stat(rp.c_str(), &st) == 0);
    if (!ensure_dir(rp, 0755)) return false;
    for (const char *dir : skeleton_dirs) {
        if (!ensure_dir(rp + "/" + dir, 0755)) return false;
    }
    if (chmod((rp + "/tmp").c_str(), 01777) != 0) return false;
    if (chmod((rp + "/var/tmp").c_str(), 01777) != 0) return false;
    if (chmod((rp + "/dev/shm").c_str(), 01777) != 0) return false;
    if (chmod((rp + "/root").c_str(), 0700) != 0) return false;
    make_symlink("/proc/self/fd", rp + "/dev/fd");
    make_symlink("/proc/self/fd/0", rp + "/dev/stdin");
    make_symlink("/proc/self/fd/1", rp + "/dev/stdout");
    make_symlink("/proc/self/fd/2", rp + "/dev/stderr");
    make_symlink("pts/ptmx", rp + "/dev/ptmx");
    std::cout << "mroot: " << (existed ? "root already initialized at " : "initialized root ")
              << index_ << " at " << rp << "\n";
    return true;
}

bool MultiRoot::mount_all(bool dns) {
    if (is_host()) {
        std::cout << "mroot: index 0 is the host root, nothing to mount\n";
        return true;
    }
    if (!require_root()) return false;
    std::string rp = work_path(*this);
    if (rp.empty()) return false;
    for (const MountSpec &m : vfs_mounts) {
        std::string target = join_root(rp, m.target);
        if (is_mounted(target)) continue;
        if (!ensure_dir(target, 0755)) continue;
        if (mount(m.source, target.c_str(), m.fstype, m.flags, m.data) != 0) {
            if (errno != EBUSY) {
                std::cerr << "mroot: mount " << m.fstype << " on " << target << ": "
                          << strerror(errno) << "\n";
            }
        }
    }
    if (dns) mount_dns(rp);
    return true;
}

bool MultiRoot::umount_all() {
    if (is_host()) {
        std::cout << "mroot: index 0 is the host root, nothing to unmount\n";
        return true;
    }
    if (!require_root()) return false;
    std::string rp = work_path(*this);
    if (rp.empty()) return false;
    for (const char *target : umount_targets) {
        std::string t = join_root(rp, target);
        if (umount2(t.c_str(), MNT_DETACH) != 0) {
            if (errno != EINVAL && errno != ENOENT) {
                std::cerr << "mroot: umount " << t << ": " << strerror(errno) << "\n";
            }
        }
    }
    return true;
}

bool MultiRoot::status(std::ostream &os, bool detailed) {
    os << "index: " << index_ << "\n";
    os << "path:  " << path_ << "\n";
    std::string rp = path_;
    if (is_media()) {
        std::string cur = mountpoint_of_device(media_.device);
        rp = (cur.empty() ? media_.mountpoint : cur) + "/" + media_.relpath;
        os << "media: " << media_.device;
        if (!media_.fstype.empty()) os << " (" << media_.fstype << ")";
        os << (cur.empty() ? ", not mounted" : ", mounted") << "\n";
        os << "real-path: " << rp << "\n";
    }
    struct stat st;
    if (stat(rp.c_str(), &st) != 0) {
        os << "state: missing\n";
        return true;
    }
    os << "state: present\n";
    os << "mounts:\n";
    for (const MountSpec &m : vfs_mounts) {
        os << "  " << (is_mounted(join_root(rp, m.target)) ? "[x] " : "[ ] ")
           << join_root(rp, m.target) << "\n";
    }
    if (detailed) {
        std::string du = capture_command("du -sh \"" + rp + "\" 2>/dev/null");
        while (!du.empty() && (du.back() == '\n' || du.back() == ' ' || du.back() == '\t')) du.pop_back();
        if (!du.empty()) os << "size: " << du << "\n";
        os << "media-backed: " << (is_media() ? "yes" : "no") << "\n";
    }
    return true;
}

int MultiRoot::enter(const std::string &cmd, const std::vector<std::string> &args,
                     bool new_session, bool dns) {
    if (!require_root()) return 1;
    std::string rp = path_;
    if (!is_host()) {
        if (!init()) return 1;
        if (!mount_all(dns)) return 1;
        rp = status_path();
    }
    if (chroot(rp.c_str()) != 0) {
        std::cerr << "mroot: chroot(" << rp << "): " << strerror(errno) << "\n";
        return 1;
    }
    if (chdir("/") != 0) {
        std::cerr << "mroot: chdir(/): " << strerror(errno) << "\n";
        return 1;
    }
    return exec_prepared(cmd, args, new_session);
}

bool MultiRoot::mount_dns(const std::string &rp) const {
    std::string host_conf = "/etc/resolv.conf";
    std::string target = join_root(rp, "/etc/resolv.conf");
    if (access(host_conf.c_str(), F_OK) != 0) return true;
    if (!ensure_dir(join_root(rp, "/etc"), 0755)) return false;
    if (is_mounted(target)) return true;
    if (access(target.c_str(), F_OK) != 0) {
        int fd = open(target.c_str(), O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) close(fd);
    }
    if (mount(host_conf.c_str(), target.c_str(), nullptr, MS_BIND, nullptr) != 0) {
        std::cerr << "mroot: bind mount " << target << ": " << strerror(errno) << "\n";
        return false;
    }
    return true;
}

std::string work_path(MultiRoot &r) {
    if (!r.is_media()) return r.path();
    if (!attach_media(r.media())) return "";
    return r.media().mountpoint + "/" + r.media().relpath;
}

unsigned lowest_free_root_index() {
    for (unsigned i = ROOT_MIN + 1; i <= ROOT_MAX; ++i) {
        std::string p = roots_dir() + "/" + std::to_string(i);
        struct stat st;
        if (lstat(p.c_str(), &st) != 0) return i;
    }
    return 0;
}

bool index_for_stratum(const std::string &name, unsigned &idx) {
    std::string target = std::string("/bedrock/strata/") + name;
    for (unsigned i = ROOT_MIN + 1; i <= ROOT_MAX; ++i) {
        std::string p = roots_dir() + "/" + std::to_string(i);
        char buf[4096];
        ssize_t n = readlink(p.c_str(), buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            if (target == buf) {
                idx = i;
                return true;
            }
        }
    }
    return false;
}

bool list_strata() {
    std::vector<std::string> names = bedrock_strata();
    if (names.empty()) {
        std::cout << "mroot: no Bedrock strata (/bedrock/strata not present)\n";
        return true;
    }
    std::cout << "stratum\n";
    for (const std::string &n : names) std::cout << n << "\n";
    return true;
}

bool rmtree(const std::string &path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        if (errno == ENOENT) return true;
        return false;
    }
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        DIR *d = opendir(path.c_str());
        if (!d) {
            std::cerr << "mroot: opendir " << path << ": " << strerror(errno) << "\n";
            return false;
        }
        struct dirent *e;
        bool ok = true;
        while ((e = readdir(d))) {
            if (!ok) break;
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (!rmtree(path + "/" + e->d_name)) ok = false;
        }
        closedir(d);
        if (!ok) return false;
        if (rmdir(path.c_str()) != 0) {
            std::cerr << "mroot: rmdir " << path << ": " << strerror(errno) << "\n";
            return false;
        }
        return true;
    }
    if (unlink(path.c_str()) != 0) {
        std::cerr << "mroot: unlink " << path << ": " << strerror(errno) << "\n";
        return false;
    }
    return true;
}

bool copy_file(const std::string &src, const std::string &dst, const struct stat &st) {
    int in = open(src.c_str(), O_RDONLY | O_NOFOLLOW);
    if (in < 0) {
        std::cerr << "mroot: open " << src << ": " << strerror(errno) << "\n";
        return false;
    }
    int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (out < 0) {
        std::cerr << "mroot: open " << dst << ": " << strerror(errno) << "\n";
        close(in);
        return false;
    }
    char buf[65536];
    bool ok = true;
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n < 0) {
            std::cerr << "mroot: read " << src << ": " << strerror(errno) << "\n";
            ok = false;
            break;
        }
        if (n == 0) break;
        char *p = buf;
        while (n > 0) {
            ssize_t w = write(out, p, n);
            if (w < 0) {
                std::cerr << "mroot: write " << dst << ": " << strerror(errno) << "\n";
                ok = false;
                break;
            }
            p += w;
            n -= w;
        }
        if (!ok) break;
    }
    if (ok) {
        fchmod(out, st.st_mode & 07777);
        int rc = fchown(out, st.st_uid, st.st_gid);
        (void)rc;
        struct timespec ts[2] = { st.st_atim, st.st_mtim };
        futimens(out, ts);
    }
    close(out);
    close(in);
    return ok;
}

bool copy_tree(const std::string &src, const std::string &dst,
               std::map<std::string, std::string> &links, bool top) {
    DIR *d = opendir(src.c_str());
    if (!d) {
        std::cerr << "mroot: opendir " << src << ": " << strerror(errno) << "\n";
        return false;
    }
    if (!ensure_dir(dst, 0755)) {
        closedir(d);
        return false;
    }
    struct dirent *e;
    bool ok = true;
    while ((e = readdir(d))) {
        if (!ok) break;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (top) {
            bool skip = false;
            for (const char *s : skip_names) {
                if (!strcmp(e->d_name, s)) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
        }
        std::string s = src + "/" + e->d_name;
        std::string t = dst + "/" + e->d_name;
        struct stat st;
        if (lstat(s.c_str(), &st) != 0) {
            std::cerr << "mroot: lstat " << s << ": " << strerror(errno) << "\n";
            ok = false;
            break;
        }
        if (S_ISLNK(st.st_mode)) {
            char buf[4096];
            ssize_t n = readlink(s.c_str(), buf, sizeof(buf) - 1);
            if (n < 0) {
                std::cerr << "mroot: readlink " << s << ": " << strerror(errno) << "\n";
                ok = false;
                break;
            }
            buf[n] = '\0';
            if (symlink(buf, t.c_str()) != 0 && errno != EEXIST) {
                std::cerr << "mroot: symlink " << t << ": " << strerror(errno) << "\n";
                ok = false;
                break;
            }
            int rc = lchown(t.c_str(), st.st_uid, st.st_gid);
            (void)rc;
        } else if (S_ISDIR(st.st_mode)) {
            if (!copy_tree(s, t, links, false)) {
                ok = false;
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            char key[64];
            snprintf(key, sizeof(key), "%llu:%llu",
                     (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
            auto it = links.find(key);
            if (it != links.end()) {
                if (link(it->second.c_str(), t.c_str()) != 0 && errno != EEXIST) {
                    std::cerr << "mroot: link " << t << ": " << strerror(errno) << "\n";
                    ok = false;
                    break;
                }
                continue;
            }
            links[key] = t;
            if (!copy_file(s, t, st)) {
                ok = false;
                break;
            }
        } else if (S_ISFIFO(st.st_mode) || S_ISCHR(st.st_mode) ||
                   S_ISBLK(st.st_mode) || S_ISSOCK(st.st_mode)) {
            if (mknod(t.c_str(), st.st_mode, st.st_rdev) != 0 && errno != EEXIST) {
                std::cerr << "mroot: mknod " << t << ": " << strerror(errno) << "\n";
                ok = false;
                break;
            }
            chmod(t.c_str(), st.st_mode & 07777);
            int rc = chown(t.c_str(), st.st_uid, st.st_gid);
            (void)rc;
        }
    }
    closedir(d);
    return ok;
}

bool list_roots() {
    std::cout << "index\tpath\tmedia\n";
    for (unsigned i = ROOT_MIN; i <= ROOT_MAX; ++i) {
        std::string p = i == 0 ? "/" : roots_dir() + "/" + std::to_string(i);
        MediaConf c;
        bool has = i == 0 || access(p.c_str(), F_OK) == 0 || load_media_conf(i, c);
        if (!has) continue;
        std::cout << i << "\t" << p;
        if (c.valid) std::cout << "\t" << c.device;
        std::cout << "\n";
    }
    return true;
}

bool list_media() {
    DIR *d = opendir("/sys/class/block");
    if (!d) {
        std::cerr << "mroot: cannot read /sys/class/block\n";
        return false;
    }
    struct dirent *e;
    std::vector<std::string> devs;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char buf[8] = {0};
        int fd = open(("/sys/class/block/" + std::string(e->d_name) + "/removable").c_str(), O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0 && buf[0] == '1') devs.push_back(e->d_name);
    }
    closedir(d);
    std::sort(devs.begin(), devs.end());
    if (devs.empty()) {
        std::cout << "mroot: no removable media found\n";
        return true;
    }
    std::cout << "device\tlabel\tfstype\tsize\tmountpoint\n";
    for (const std::string &name : devs) {
        std::string dev = "/dev/" + name;
        std::string label = label_of(dev);
        std::string fstype = fs_type_of(dev);
        std::string mp = mountpoint_of_device(dev);
        std::string size = "?";
        char buf[32] = {0};
        int fd = open(("/sys/class/block/" + name + "/size").c_str(), O_RDONLY);
        if (fd >= 0) {
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) size = human_size(strtoull(buf, nullptr, 10) * 512);
        }
        std::cout << dev << "\t" << (label.empty() ? "-" : label) << "\t"
                  << (fstype.empty() ? "-" : fstype) << "\t" << size << "\t"
                  << (mp.empty() ? "-" : mp) << "\n";
    }
    return true;
}

bool clone_root(unsigned src, unsigned dst, const std::string &dst_media) {
    if (src == dst) {
        std::cerr << "mroot: source and destination are the same\n";
        return false;
    }
    if (!require_root()) return false;
    MultiRoot s(src);
    if (s.is_host()) {
        std::cerr << "mroot: cannot clone the host root\n";
        return false;
    }
    std::string sp = work_path(s);
    if (sp.empty()) return false;
    MultiRoot d(dst);
    if (d.is_host()) {
        std::cerr << "mroot: cannot clone into the host root\n";
        return false;
    }
    if (!dst_media.empty() && !init_media_root(dst, dst_media)) return false;
    if (!d.init()) return false;
    std::string dp = work_path(d);
    if (dp.empty()) return false;
    std::map<std::string, std::string> links;
    if (!copy_tree(sp, dp, links, true)) {
        std::cerr << "mroot: clone failed\n";
        return false;
    }
    std::cout << "mroot: cloned root " << src << " to root " << dst << "\n";
    return true;
}

bool remove_root(MultiRoot &r, bool force) {
    if (r.is_host()) {
        std::cerr << "mroot: cannot remove the host root\n";
        return false;
    }
    if (!require_root()) return false;
    std::string rp = work_path(r);
    if (rp.empty()) return false;
    if (!force) {
        std::cout << "mroot: delete root " << r.index() << " at " << rp << "? [y/N] " << std::flush;
        std::string line;
        std::getline(std::cin, line);
        if (line != "y" && line != "Y") {
            std::cout << "mroot: aborted\n";
            return false;
        }
    }
    r.umount_all();
    if (!rmtree(rp)) {
        std::cerr << "mroot: failed to remove " << rp << "\n";
        return false;
    }
    if (r.is_media()) remove_media_conf(r.index());
    std::cout << "mroot: removed root " << r.index() << "\n";
    return true;
}

bool snapshot_root(unsigned index, const std::string &file) {
    if (!require_root()) return false;
    MultiRoot r(index);
    if (r.is_host()) {
        std::cerr << "mroot: cannot snapshot the host root\n";
        return false;
    }
    std::string rp = work_path(r);
    if (rp.empty()) return false;
    std::string out = file;
    if (out.empty()) {
        std::string dir = roots_dir() + "/.mroot/backups";
        ensure_dir(dir, 0755);
        out = dir + "/root-" + std::to_string(index) + "-" + timestamp() + ".tar.gz";
    }
    std::vector<std::string> args = {
        "tar", "-czf", out, "-C", rp,
        "--exclude=./proc", "--exclude=./sys", "--exclude=./dev",
        "--exclude=./run", "--exclude=./tmp", "."
    };
    if (!run_command(args)) {
        std::cerr << "mroot: snapshot failed\n";
        return false;
    }
    std::cout << "mroot: root " << index << " snapshot written to " << out << "\n";
    return true;
}

bool restore_root(unsigned index, const std::string &file) {
    if (!require_root()) return false;
    MultiRoot r(index);
    if (r.is_host()) {
        std::cerr << "mroot: cannot restore the host root\n";
        return false;
    }
    if (access(file.c_str(), F_OK) != 0) {
        std::cerr << "mroot: no such archive: " << file << "\n";
        return false;
    }
    if (!r.init()) return false;
    std::string rp = work_path(r);
    if (rp.empty()) return false;
    if (!run_command({ "tar", "-xzf", file, "-C", rp })) {
        std::cerr << "mroot: restore failed\n";
        return false;
    }
    std::cout << "mroot: root " << index << " restored from " << file << "\n";
    return true;
}

bool attach_root(MultiRoot &r) {
    if (!r.is_media()) {
        std::cout << "mroot: root " << r.index() << " is not media-backed\n";
        return false;
    }
    if (!require_root()) return false;
    if (!attach_media(r.media())) return false;
    std::cout << "mroot: root " << r.index() << " device " << r.media().device
              << " attached at " << r.media().mountpoint << "\n";
    return true;
}

bool detach_root(MultiRoot &r) {
    if (!r.is_media()) {
        std::cout << "mroot: root " << r.index() << " is not media-backed\n";
        return false;
    }
    if (!require_root()) return false;
    if (!detach_media(r.media())) return false;
    std::cout << "mroot: root " << r.index() << " device " << r.media().device
              << " detached\n";
    return true;
}

bool login_user(const std::string &user, unsigned index) {
    if (!require_root()) return false;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) {
        std::cerr << "mroot: cannot locate mroot binary: " << strerror(errno) << "\n";
        return false;
    }
    exe[n] = '\0';
    std::string wrap = "/usr/local/bin/mroot-enter-" + std::to_string(index);
    FILE *f = fopen(wrap.c_str(), "w");
    if (!f) {
        std::cerr << "mroot: cannot create " << wrap << ": " << strerror(errno) << "\n";
        return false;
    }
    fprintf(f, "#!/bin/sh\nexec %s enter %u \"$@\"\n", exe, index);
    fclose(f);
    chmod(wrap.c_str(), 0755);
    if (!run_command({ "usermod", "-s", wrap, user })) {
        std::cerr << "mroot: could not set the login shell for user " << user << "\n";
        return false;
    }
    std::cout << "mroot: user " << user << " will enter root " << index << " at login\n";
    return true;
}

} // namespace mroot
