#ifndef MROOT_MEDIA_H
#define MROOT_MEDIA_H

#include "config.h"
#include <string>

namespace mroot {

std::string mountpoint_of_device(const std::string &dev);
std::string fs_type_of(const std::string &dev);
std::string label_of(const std::string &dev);
bool load_media_conf(unsigned index, MediaConf &conf);
bool save_media_conf(const MediaConf &conf, unsigned index);
void remove_media_conf(unsigned index);
bool attach_media(MediaConf &conf);
bool detach_media(const MediaConf &conf);
bool init_media_root(unsigned index, const std::string &dev);

} // namespace mroot

#endif // MROOT_MEDIA_H
