#ifndef MROOT_ROOTS_H
#define MROOT_ROOTS_H

#include "config.h"
#include <iostream>
#include <string>
#include <vector>

namespace mroot {

class MultiRoot;

std::string work_path(MultiRoot &r);

class MultiRoot {
public:
    explicit MultiRoot(unsigned index);

    unsigned index() const;
    const std::string &path() const;
    bool is_host() const;
    bool is_media() const;
    bool is_stratum() const;
    MediaConf &media();

    std::string status_path() const;
    bool init();
    bool mount_all(bool dns);
    bool umount_all();
    bool status(std::ostream &os, bool detailed);
    int enter(const std::string &cmd, const std::vector<std::string> &args,
              bool new_session, bool dns);

private:
    bool mount_dns(const std::string &rp) const;

    unsigned index_;
    std::string path_;
    MediaConf media_;
};

unsigned lowest_free_root_index();
bool index_for_stratum(const std::string &name, unsigned &idx);
bool list_strata();

bool list_roots();
bool list_media();
bool clone_root(unsigned src, unsigned dst, const std::string &dst_media);
bool remove_root(MultiRoot &r, bool force);
bool snapshot_root(unsigned index, const std::string &file);
bool restore_root(unsigned index, const std::string &file);
bool attach_root(MultiRoot &r);
bool detach_root(MultiRoot &r);
bool login_user(const std::string &user, unsigned index);

#if defined(__linux__) || defined(__LINUX__)
    namespace funroot_k {
        bool available();
    }
#endif

} // namespace mroot

#endif // MROOT_ROOTS_H
