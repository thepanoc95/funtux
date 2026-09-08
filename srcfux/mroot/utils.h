#ifndef MROOT_UTILS_H
#define MROOT_UTILS_H

#include <string>
#include <vector>

namespace mroot {

bool ensure_dir(const std::string &path, mode_t mode = 0755);
bool require_root();
bool parse_index(const char *s, unsigned &out);
std::string basename_of(const std::string &p);
void make_symlink(const std::string &target, const std::string &link);
bool is_mounted(const std::string &path);
std::string join_root(const std::string &rp, const std::string &p);
std::string capture_command(const std::string &cmd);
bool run_command(const std::vector<std::string> &args);
std::string human_size(unsigned long long bytes);
std::string timestamp();

} // namespace mroot

#endif // MROOT_UTILS_H
