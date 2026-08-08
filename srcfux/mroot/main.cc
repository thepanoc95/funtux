#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#define _FUNTUX_VERSION version
#define _MROOT_VERSION _FUNTUX_VERSION

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <map>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include "../../version.h"

#if defined(__linux__) || defined(__LINUX__)
    #include <sys/ioctl.h>
    #include <sys/mount.h>
    #include <sys/syscall.h>
    #include <sched.h>
    #include "funroot.h"
#endif

namespace mroot {

constexpr unsigned ROOT_MIN = 0;
constexpr unsigned ROOT_MAX = 255;
constexpr const char *ROOTS_DIR = "/var/roots";

struct MountSpec {
    const char *source;
    const char *target;
    const char *fstype;
    unsigned long flags;
    const char *data;
};

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

struct MediaConf {
    std::string device;
    std::string mountpoint;
    std::string relpath;
    std::string fstype;
    bool valid = false;
};

std::string roots_dir() {
    if (const char *env = getenv("MROOT_ROOTS_DIR")) return env;
    return ROOTS_DIR;
}

bool ensure_dir(const std::string &path, mode_t mode);

bool bedrock_detected() {
    struct stat st;
    return stat("/bedrock/strata", &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<std::string> bedrock_strata() {
    std::vector<std::string> names;
    DIR *d = opendir("/bedrock/strata");
    if (!d) return names;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        std::string full = std::string("/bedrock/strata/") + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            names.push_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    return names;
}

/* lowest free index in [1,255]; 0 = table full */
unsigned lowest_free_root_index() {
    for (unsigned i = ROOT_MIN + 1; i <= ROOT_MAX; ++i) {
        std::string p = roots_dir() + "/" + std::to_string(i);
        struct stat st;
        if (lstat(p.c_str(), &st) != 0) return i;
    }
    return 0;
}

/* index a stratum is already mapped to, if any */
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

/* symlink each bedrock stratum to a root index; existing mappings stay */
bool bedrock_sync() {
    if (!bedrock_detected()) {
        std::cout << "mroot: no Bedrock strata (/bedrock/strata not present)\n";
        return true;
    }
    std::vector<std::string> names = bedrock_strata();
    std::string rd = roots_dir();
    if (!ensure_dir(rd, 0755)) return false;
    unsigned added = 0;
    for (const std::string &name : names) {
        unsigned idx;
        if (index_for_stratum(name, idx)) {
            std::cout << "mroot: stratum " << name
                      << " already mapped to root " << idx << "\n";
            continue;
        }
        idx = lowest_free_root_index();
        if (idx == 0) {
            std::cerr << "mroot: no free root index for stratum " << name << "\n";
            return false;
        }
        std::string link = rd + "/" + std::to_string(idx);
        std::string target = "/bedrock/strata/" + name;
        if (symlink(target.c_str(), link.c_str()) != 0 && errno != EEXIST) {
            std::cerr << "mroot: cannot link " << link << " -> " << target << ": "
                      << strerror(errno) << "\n";
            return false;
        }
        std::cout << "mroot: mapped stratum " << name << " -> root " << idx
                  << " (" << target << ")\n";
        added++;
    }
    std::cout << "mroot: bedrock sync complete (" << added << " new mapping(s), "
              << names.size() << " stratum/strata total)\n";
    return true;
}

bool require_root() {
    if (geteuid() != 0) {
        std::cerr << "mroot: this operation requires root privileges\n";
        return false;
    }
    return true;
}

bool parse_index(const char *s, unsigned &out) {
    if (!s || !*s) return false;
    char *end = nullptr;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return false;
    if (v < (long)ROOT_MIN || v > (long)ROOT_MAX) return false;
    out = (unsigned)v;
    return true;
}

std::string basename_of(const std::string &p) {
    size_t pos = p.find_last_of('/');
    return pos == std::string::npos ? p : p.substr(pos + 1);
}

bool ensure_dir(const std::string &path, mode_t mode = 0755) {
    if (mkdir(path.c_str(), mode) == 0) return true;
    if (errno == EEXIST) return true;
    if (errno == ENOENT) {
        size_t pos = path.rfind('/');
        if (pos != std::string::npos && pos > 0) {
            if (!ensure_dir(path.substr(0, pos), 0755)) return false;
        }
        if (mkdir(path.c_str(), mode) == 0) return true;
        if (errno == EEXIST) return true;
    }
    std::cerr << "mroot: cannot create directory " << path << ": "
              << strerror(errno) << "\n";
    return false;
}

void make_symlink(const std::string &target, const std::string &link) {
    if (symlink(target.c_str(), link.c_str()) != 0 && errno != EEXIST) {
        std::cerr << "mroot: symlink " << link << " -> " << target << ": "
                  << strerror(errno) << "\n";
    }
}

bool is_mounted(const std::string &path) {
    FILE *f = fopen("/proc/self/mountinfo", "r");
    if (!f) return false;
    char line[4096];
    bool found = false;
    std::string needle = path + " ";
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle.c_str())) {
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

std::string join_root(const std::string &rp, const std::string &p) {
    return rp == "/" ? p : rp + p;
}

std::string mountpoint_of_device(const std::string &dev) {
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return "";
    char line[4096];
    std::string res;
    while (fgets(line, sizeof(line), f)) {
        char d[512], m[4096];
        if (sscanf(line, "%511s %4095s", d, m) == 2 && dev == d) {
            res = m;
            break;
        }
    }
    fclose(f);
    return res;
}

std::string capture_command(const std::string &cmd) {
    FILE *f = popen(cmd.c_str(), "r");
    if (!f) return "";
    char buf[1024];
    std::string out;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    pclose(f);
    return out;
}

bool run_command(const std::vector<std::string> &args) {
    std::vector<char *> argv;
    for (const std::string &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "mroot: fork: " << strerror(errno) << "\n";
        return false;
    }
    if (pid == 0) {
        execvp(argv[0], argv.data());
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0) {
        std::cerr << "mroot: waitpid: " << strerror(errno) << "\n";
        return false;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) return false;
    return true;
}

#if defined(__linux__) || defined(__LINUX__)
    void make_session() {
        if (getsid(0) == getpid()) return;
        if (setsid() < 0) {
            if (errno == EPERM) {
                pid_t pid = fork();
                if (pid > 0) _exit(0);
                if (pid < 0) return;
                if (setsid() < 0) return;
            } else {
                return;
            }
        }
        if (isatty(0)) ioctl(0, TIOCSCTTY, 0);
    }

    int exec_prepared(const std::string &cmd, const std::vector<std::string> &args,
                      bool new_session) {
        char term[64] = {0};
        if (const char *t = getenv("TERM")) strncpy(term, t, sizeof(term) - 1);
        clearenv();
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("HOME", "/root", 1);
        setenv("TERM", term[0] ? term : "linux", 1);
        setenv("SHELL", cmd.c_str(), 1);
        setenv("USER", "root", 1);
        setenv("LOGNAME", "root", 1);
        if (new_session) make_session();
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(cmd.c_str()));
        for (const std::string &a : args) argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        std::cerr << "mroot: exec " << cmd << ": " << strerror(errno) << "\n";
        return 127;
    }
#endif // __linux__

std::string fs_type_of(const std::string &dev) {
    std::string out = capture_command("blkid -s TYPE -o value " + dev + " 2>/dev/null");
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out;
}

std::string label_of(const std::string &dev) {
    std::string out = capture_command("blkid -s LABEL -o value " + dev + " 2>/dev/null");
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out;
}

std::string human_size(unsigned long long bytes) {
    static const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        u++;
    }
    char buf[64];
    if (u == 0) snprintf(buf, sizeof(buf), "%.0f%s", v, units[u]);
    else snprintf(buf, sizeof(buf), "%.1f%s", v, units[u]);
    return buf;
}

std::string timestamp() {
    char buf[64];
    time_t t = time(nullptr);
    struct tm *tm = localtime(&t);
    strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", tm);
    return buf;
}

std::string media_conf_path(unsigned index) {
    return roots_dir() + "/.mroot/" + std::to_string(index) + ".conf";
}

bool load_media_conf(unsigned index, MediaConf &conf) {
    FILE *f = fopen(media_conf_path(index).c_str(), "r");
    if (!f) return false;
    char line[1024];
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = s.substr(0, eq);
        std::string v = s.substr(eq + 1);
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
        if (k == "device") conf.device = v;
        else if (k == "mountpoint") conf.mountpoint = v;
        else if (k == "relpath") conf.relpath = v;
        else if (k == "fstype") conf.fstype = v;
        ok = true;
    }
    fclose(f);
    conf.valid = ok && !conf.device.empty();
    return conf.valid;
}

bool save_media_conf(const MediaConf &conf, unsigned index) {
    std::string dir = roots_dir() + "/.mroot";
    if (!ensure_dir(dir, 0755)) return false;
    FILE *f = fopen(media_conf_path(index).c_str(), "w");
    if (!f) {
        std::cerr << "mroot: cannot write " << media_conf_path(index) << ": "
                  << strerror(errno) << "\n";
        return false;
    }
    fprintf(f, "index=%u\ndevice=%s\nmountpoint=%s\nrelpath=%s\nfstype=%s\n",
            index, conf.device.c_str(), conf.mountpoint.c_str(),
            conf.relpath.c_str(), conf.fstype.c_str());
    fclose(f);
    return true;
}

void remove_media_conf(unsigned index) {
    unlink(media_conf_path(index).c_str());
    rmdir((roots_dir() + "/.mroot").c_str());
}

bool attach_media(MediaConf &conf) {
    std::string cur = mountpoint_of_device(conf.device);
    if (!cur.empty()) {
        conf.mountpoint = cur;
        return true;
    }
    if (conf.mountpoint.empty()) conf.mountpoint = "/run/media/mroot/" + basename_of(conf.device);
    if (!ensure_dir(conf.mountpoint, 0755)) return false;
    std::vector<std::string> args = { "mount" };
    if (!conf.fstype.empty()) {
        args.push_back("-t");
        args.push_back(conf.fstype);
    }
    args.push_back(conf.device);
    args.push_back(conf.mountpoint);
    if (!run_command(args)) {
        std::cerr << "mroot: could not attach " << conf.device << " at " << conf.mountpoint << "\n";
        return false;
    }
    return true;
}

bool detach_media(const MediaConf &conf) {
    std::string cur = mountpoint_of_device(conf.device);
    if (cur.empty()) return true;
    if (!run_command({ "umount", cur })) {
        std::cerr << "mroot: could not detach " << conf.device << " (device busy?)\n";
        return false;
    }
    return true;
}

bool init_media_root(unsigned index, const std::string &dev) {
    if (index == 0) {
        std::cerr << "mroot: the host root cannot be on removable media\n";
        return false;
    }
    if (!require_root()) return false;
    struct stat st;
    if (stat(dev.c_str(), &st) != 0 || !S_ISBLK(st.st_mode)) {
        std::cerr << "mroot: " << dev << " is not a block device\n";
        return false;
    }
    MediaConf conf;
    conf.device = dev;
    conf.mountpoint = mountpoint_of_device(dev);
    if (conf.mountpoint.empty()) conf.mountpoint = "/run/media/mroot/" + basename_of(dev);
    conf.relpath = "mroot/" + std::to_string(index);
    conf.fstype = fs_type_of(dev);
    if (!attach_media(conf)) return false;
    std::string rp = conf.mountpoint + "/" + conf.relpath;
    if (!ensure_dir(rp, 0755)) return false;
    if (!save_media_conf(conf, index)) return false;
    std::cout << "mroot: root " << index << " will live on " << dev << " at " << rp;
    if (!conf.fstype.empty()) std::cout << " (" << conf.fstype << ")";
    std::cout << "\n";
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

class MultiRoot;

std::string work_path(MultiRoot &r);

class MultiRoot {
public:
    explicit MultiRoot(unsigned index)
        : index_(index),
          path_(index == 0 ? "/" : roots_dir() + "/" + std::to_string(index)) {
        load_media_conf(index_, media_);
    }

    unsigned index() const { return index_; }
    const std::string &path() const { return path_; }
    bool is_host() const { return index_ == 0; }
    bool is_media() const { return media_.valid; }
    bool is_stratum() const {
        std::string rp = is_media() ? status_path() : path_;
        return rp.compare(0, sizeof("/bedrock/strata/") - 1, "/bedrock/strata/") == 0;
    }
    MediaConf &media() { return media_; }

    std::string status_path() const {
        if (!media_.valid) return path_;
        return media_.mountpoint + "/" + media_.relpath;
    }

    bool init() {
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

    bool mount_all(bool dns) {
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

    bool umount_all() {
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

    bool status(std::ostream &os, bool detailed) {
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

    int enter(const std::string &cmd, const std::vector<std::string> &args,
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

private:
    bool mount_dns(const std::string &rp) const {
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

    unsigned index_;
    std::string path_;
    MediaConf media_;
};

std::string work_path(MultiRoot &r) {
    if (!r.is_media()) return r.path();
    if (!attach_media(r.media())) return "";
    return r.media().mountpoint + "/" + r.media().relpath;
}

#if defined(__linux__) || defined(__LINUX__)
    namespace funroot_k {

        constexpr const char *DEV = "/dev/funroot";

        int open_dev() {
            return ::open(DEV, O_RDWR | O_CLOEXEC);
        }

        bool available() {
            int fd = open_dev();
            if (fd < 0) return false;
            close(fd);
            return true;
        }

        int add(unsigned index, const std::string &path) {
            int fd = open_dev();
            if (fd < 0) return -1;
            struct funroot_req req{};
            req.index = index;
            strncpy(req.path, path.c_str(), sizeof(req.path) - 1);
            int rc = ioctl(fd, FUNROOT_ADD, &req);
            if (rc != 0 && errno == EBUSY) rc = 0;
            close(fd);
            return rc;
        }

        int del(unsigned index) {
            int fd = open_dev();
            if (fd < 0) return -1;
            struct funroot_req req{};
            req.index = index;
            int rc = ioctl(fd, FUNROOT_DEL, &req);
            close(fd);
            return rc;
        }

        int set(unsigned index) {
            int fd = open_dev();
            if (fd < 0) return -1;
            struct funroot_req req{};
            req.index = index;
            int rc = ioctl(fd, FUNROOT_SET, &req);
            close(fd);
            return rc;
        }

        int get(unsigned &index, std::string &path) {
            int fd = open_dev();
            if (fd < 0) return -1;
            struct funroot_req req{};
            int rc = ioctl(fd, FUNROOT_GET, &req);
            close(fd);
            if (rc == 0) {
                index = req.index;
                path = req.path;
            }
            return rc;
        }

        bool list(std::ostream &os) {
            int fd = open_dev();
            if (fd < 0) return false;
            os << "index\tpath\n";
            for (unsigned i = 0; i < FUNROOT_MAX_ROOTS; ++i) {
                struct funroot_req req{};
                req.index = i;
                if (ioctl(fd, FUNROOT_PATH, &req) == 0)
                    os << i << "\t" << req.path << "\n";
            }
            close(fd);
            return true;
        }

    } // namespace funroot_k

    bool kernel_enter(unsigned index, const std::string &cmd,
                      const std::vector<std::string> &args,
                      bool new_session, bool dns) {
        if (!require_root()) return false;
        if (!funroot_k::available()) {
            std::cerr << "mroot: funroot kernel module not loaded (/dev/funroot)\n";
            return false;
        }
        MultiRoot r(index);
        std::string rp;
        if (r.is_host()) {
            rp = "/";
        } else {
            if (!r.init()) return false;
            if (!r.mount_all(dns)) return false;
            rp = r.status_path();
        }
        if (funroot_k::add(index, rp) != 0) {
            std::cerr << "mroot: cannot register root " << index << " with funroot\n";
            return false;
        }
        if (funroot_k::set(index) != 0) {
            std::cerr << "mroot: funroot switch to root " << index << " failed\n";
            return false;
        }
        if (chdir("/") != 0) {
            std::cerr << "mroot: chdir(/): " << strerror(errno) << "\n";
            return false;
        }
        return exec_prepared(cmd, args, new_session) == 0;
    }

    bool kernel_switch(unsigned index) {
        if (!require_root()) return false;
        if (!funroot_k::available()) {
            std::cerr << "mroot: funroot kernel module not loaded (/dev/funroot)\n";
            return false;
        }
        MultiRoot r(index);
        std::string rp = r.is_host() ? "/" : r.status_path();
        if (funroot_k::add(index, rp) != 0) {
            std::cerr << "mroot: cannot register root " << index << " with funroot\n";
            return false;
        }
        if (funroot_k::set(index) != 0) {
            std::cerr << "mroot: funroot switch to root " << index << " failed\n";
            return false;
        }
        std::cout << "mroot: switched this process's root to " << index
                  << " (" << rp << ")\n";
        return true;
    }

    bool kernel_sync() {
        if (!funroot_k::available()) {
            std::cerr << "mroot: funroot kernel module not loaded (/dev/funroot)\n";
            return false;
        }
        if (!bedrock_sync()) return false;
        unsigned ok = 0;
        for (unsigned i = ROOT_MIN; i <= ROOT_MAX; ++i) {
            std::string p = i == 0 ? "/" : roots_dir() + "/" + std::to_string(i);
            MediaConf c;
            bool has = i == 0 || access(p.c_str(), F_OK) == 0 || load_media_conf(i, c);
            if (!has) continue;
            if (i != 0) {
                MultiRoot r(i);
                p = r.status_path();
            }
            if (funroot_k::add(i, p) == 0) {
                ok++;
                std::cout << "funroot: registered " << i << " -> " << p << "\n";
            }
        }
        std::cout << "funroot: " << ok << " root(s) registered\n";
        return true;
    }
#endif // __linux__

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

void print_usage(std::ostream &os) {
    os << "mroot " << _MROOT_VERSION << " - multiple Linux root filesystems\n\n"
       << "Usage: mroot [options] <command> [args...]\n\n"
       << "Commands:\n"
       << "  init <index> [--media <dev>]  create a root skeleton, optionally on removable media\n"
       << "  mount <index>                 mount virtual filesystems (proc, sys, dev, ...)\n"
       << "  umount <index>                unmount the virtual filesystems\n"
       << "  enter <index> [cmd...]        enter the root via the funroot kernel module (falls\n"
       << "                                  back to a session-wide chroot if it is not loaded)\n"
       << "  kenter <index> [cmd...]       enter a root via the funroot kernel module (switch + exec)\n"
       << "  kswitch <index>               switch this process's / to a root live (no exec)\n"
       << "  kget                          show this process's current funroot index/path\n"
       << "  klist                         list roots registered with the funroot kernel module\n"
       << "  ksync                         register all configured roots with the kernel module\n"
       << "  strata                        list Bedrock Linux strata\n"
       << "  bedrock-sync                  map each Bedrock stratum to a root index\n"
       << "  status <index>                show the state of a root\n"
       << "  info <index>                  show detailed state (size, media, ...)\n"
       << "  list                          list configured roots\n"
       << "  list-media                    list removable storage devices\n"
       << "  attach <index>                mount a media-backed root's device\n"
       << "  detach <index>                unmount a media-backed root's device\n"
       << "  clone <src> <dst> [--media d] copy a root (keeps links, modes, timestamps)\n"
       << "  remove <index> [-f]           delete a root and its files\n"
       << "  snapshot <index> [file]       write a gzipped tar backup of a root\n"
       << "  restore <index> <archive>     restore a root from a snapshot\n"
       << "  login <user> [index]          make a user's login enter a root (0 = host)\n\n"
       << "Roots live under " << ROOTS_DIR << "/<index> (override with $MROOT_ROOTS_DIR);\n"
       << "index 0 is the host root (/). The enter command can be used as a login shell\n"
       << "to start a whole session inside a root. Set MROOT_NO_KERNEL=1 to force the\n"
       << "chroot-based enter even when the funroot kernel module is loaded.\n\n"
       << "Bedrock Linux: `mroot bedrock-sync` maps each /bedrock/strata/<name> to a root\n"
       << "index, so switching a process's root is the same as switching its Bedrock stratum.\n\n"
       << "Options:\n"
       << "  -d, --dns          bind-mount the host's /etc/resolv.conf into the root\n"
       << "  -s, --session      create a new session (setsid) before entering\n"
       << "  -f, --force        do not ask for confirmation (remove)\n"
       << "  -m, --media <dev>  use removable device <dev> (init, clone)\n"
       << "  -h, --help         show this help and exit\n"
       << "  -V, --version      show version and exit\n";
}

void print_version() {
    std::cout << "mroot " << _MROOT_VERSION << "\n";
}

}

int main(int argc, char **argv) {
    using namespace mroot;

    static const struct option long_options[] = {
        { "help",    no_argument,       nullptr, 'h' },
        { "version", no_argument,       nullptr, 'V' },
        { "dns",     no_argument,       nullptr, 'd' },
        { "session", no_argument,       nullptr, 's' },
        { "force",   no_argument,       nullptr, 'f' },
        { "media",   required_argument, nullptr, 'm' },
        { nullptr, 0, nullptr, 0 },
    };

    bool dns = false;
    bool new_session = false;
    bool force = false;
    std::string media_dev;

    int opt;
    while ((opt = getopt_long(argc, argv, "hVdsfm:", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h': print_usage(std::cout); return 0;
            case 'V': print_version(); return 0;
            case 'd': dns = true; break;
            case 's': new_session = true; break;
            case 'f': force = true; break;
            case 'm': media_dev = optarg; break;
            default: print_usage(std::cerr); return 2;
        }
    }

    if (optind >= argc) {
        print_usage(std::cerr);
        return 2;
    }

    std::string command = argv[optind++];

    if (command == "list") return list_roots() ? 0 : 1;
    if (command == "list-media") return list_media() ? 0 : 1;
    if (command == "strata") return list_strata() ? 0 : 1;
    if (command == "bedrock-sync") return bedrock_sync() ? 0 : 1;

    if (optind >= argc) {
        std::cerr << "mroot: missing arguments for '" << command << "'\n";
        print_usage(std::cerr);
        return 2;
    }

    auto need_index = [&](unsigned &idx) -> bool {
        if (!parse_index(argv[optind++], idx)) {
            std::cerr << "mroot: invalid root index '" << argv[optind - 1]
                      << "' (expected 0-" << ROOT_MAX << ")\n";
            return false;
        }
        return true;
    };

    unsigned a, b;
    std::string sarg;

    if (command == "init" || command == "mount" || command == "umount" ||
        command == "unmount" || command == "status" || command == "info" ||
        command == "attach" || command == "detach") {
        if (!need_index(a)) return 2;
        MultiRoot r(a);
        if (command == "init") {
            if (!media_dev.empty() && !init_media_root(a, media_dev)) return 1;
            return r.init() ? 0 : 1;
        }
        if (command == "mount") return r.mount_all(dns) ? 0 : 1;
        if (command == "umount" || command == "unmount") return r.umount_all() ? 0 : 1;
        if (command == "status") return r.status(std::cout, false) ? 0 : 1;
        if (command == "info") return r.status(std::cout, true) ? 0 : 1;
        if (command == "attach") return attach_root(r) ? 0 : 1;
        if (command == "detach") return detach_root(r) ? 0 : 1;
    }

    if (command == "enter") {
        if (!need_index(a)) return 2;
        MultiRoot r(a);
        std::string cmd = "/bin/sh";
        std::vector<std::string> args;
        if (optind < argc) cmd = argv[optind++];
        while (optind < argc) args.push_back(argv[optind++]);
        /*
         * kernel-first: use funroot if loaded, else plain chroot.
         * MROOT_NO_KERNEL=1 forces chroot.
         */
        if (!getenv("MROOT_NO_KERNEL") && funroot_k::available())
            return kernel_enter(a, cmd, args, new_session, dns) ? 0 : 1;
        return r.enter(cmd, args, new_session, dns);
    }

    #if defined(__linux__) || defined(__LINUX__)
        if (command == "kenter" || command == "kswitch") {
            if (!need_index(a)) return 2;
            if (command == "kswitch") return kernel_switch(a) ? 0 : 1;
            std::string cmd = "/bin/sh";
            std::vector<std::string> args;
            if (optind < argc) cmd = argv[optind++];
            while (optind < argc) args.push_back(argv[optind++]);
            return kernel_enter(a, cmd, args, new_session, dns) ? 0 : 1;
        }

        if (command == "kget") {
            unsigned idx;
            std::string p;
            if (funroot_k::get(idx, p) != 0) {
                std::cerr << "mroot: funroot kernel module not available (/dev/funroot)\n";
                return 1;
            }
            std::cout << "index: " << idx << "\n";
            if (!p.empty()) std::cout << "path:  " << p << "\n";
            return 0;
        }

        if (command == "klist") {
            if (!funroot_k::available()) {
                std::cerr << "mroot: funroot kernel module not loaded (/dev/funroot)\n";
                return 1;
            }
            return funroot_k::list(std::cout) ? 0 : 1;
        }

        if (command == "ksync") return kernel_sync() ? 0 : 1;
    #endif

    if (command == "remove") {
        if (!need_index(a)) return 2;
        MultiRoot r(a);
        return remove_root(r, force) ? 0 : 1;
    }

    if (command == "snapshot") {
        if (!need_index(a)) return 2;
        if (optind < argc) sarg = argv[optind++];
        return snapshot_root(a, sarg) ? 0 : 1;
    }

    if (command == "restore") {
        if (!need_index(a)) return 2;
        if (optind >= argc) {
            std::cerr << "mroot: restore requires an archive file\n";
            return 2;
        }
        sarg = argv[optind++];
        return restore_root(a, sarg) ? 0 : 1;
    }

    if (command == "clone") {
        if (!need_index(a)) return 2;
        if (optind >= argc || !parse_index(argv[optind++], b)) {
            std::cerr << "mroot: clone requires <src> <dst> indices\n";
            return 2;
        }
        return clone_root(a, b, media_dev) ? 0 : 1;
    }

    if (command == "login") {
        if (optind >= argc) {
            std::cerr << "mroot: login requires a username\n";
            return 2;
        }
        std::string user = argv[optind++];
        unsigned idx = 0;
        if (optind < argc && !parse_index(argv[optind++], idx)) {
            std::cerr << "mroot: invalid root index for login\n";
            return 2;
        }
        return login_user(user, idx) ? 0 : 1;
    }

    std::cerr << "mroot: unknown command '" << command << "'\n";
    print_usage(std::cerr);
    return 2;
}
