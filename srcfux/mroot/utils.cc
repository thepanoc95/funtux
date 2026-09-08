#include "config.h"
#include "utils.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace mroot {

std::string roots_dir() {
    if (const char *env = getenv("MROOT_ROOTS_DIR")) return env;
    return ROOTS_DIR;
}

std::string media_conf_path(unsigned index) {
    return roots_dir() + "/.mroot/" + std::to_string(index) + ".conf";
}

bool ensure_dir(const std::string &path, mode_t mode) {
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

} // namespace mroot
