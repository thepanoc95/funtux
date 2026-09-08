#include "config.h"
#include "media.h"
#include "utils.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace mroot {

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

} // namespace mroot
