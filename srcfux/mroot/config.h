#ifndef MROOT_CONFIG_H
#define MROOT_CONFIG_H

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <string>

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

extern const MountSpec vfs_mounts[];
extern const char *umount_targets[];
extern const char *skeleton_dirs[];
extern const char *skip_names[];

struct MediaConf {
    std::string device;
    std::string mountpoint;
    std::string relpath;
    std::string fstype;
    bool valid = false;
};

std::string roots_dir();
std::string media_conf_path(unsigned index);

} // namespace mroot

#endif // MROOT_CONFIG_H
