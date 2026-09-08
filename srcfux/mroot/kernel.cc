#include "kernel.h"
#include "config.h"
#include "utils.h"
#include "roots.h"
#include "media.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <climits>

namespace mroot {

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

#if defined(__linux__) || defined(__LINUX__)
    #include <sys/ioctl.h>
    #include <sys/syscall.h>
    #include <sched.h>
    #include <funroot.h>
#endif

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

} // namespace mroot
