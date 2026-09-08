#define _FUNTUX_VERSION version
#define _MROOT_VERSION _FUNTUX_VERSION

#include "config.h"
#include "kernel.h"
#include "media.h"
#include "utils.h"
#include "roots.h"
#include "../../version.h"

#include <cstring>
#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

namespace mroot {

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

} // namespace mroot

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
