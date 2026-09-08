#ifndef MROOT_KERNEL_H
#define MROOT_KERNEL_H

#include <string>
#include <vector>

#if defined(__linux__) || defined(__LINUX__)
    #include <sys/ioctl.h>
#endif

namespace mroot {

bool bedrock_detected();
std::vector<std::string> bedrock_strata();
bool bedrock_sync();

#if defined(__linux__) || defined(__LINUX__)
    namespace funroot_k {
        bool available();
        int add(unsigned index, const std::string &path);
        int del(unsigned index);
        int set(unsigned index);
        int get(unsigned &index, std::string &path);
        bool list(std::ostream &os);
    }

    void make_session();
    int exec_prepared(const std::string &cmd, const std::vector<std::string> &args,
                      bool new_session);
    bool kernel_enter(unsigned index, const std::string &cmd,
                      const std::vector<std::string> &args,
                      bool new_session, bool dns);
    bool kernel_switch(unsigned index);
    bool kernel_sync();
#endif

} // namespace mroot

#endif // MROOT_KERNEL_H
