/*
 * froot.c - FunTux multiroot tool built on the libfunobject object model.
 */

#define _DEFAULT_SOURCE

#include "funroot.h"
#include "funobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FROOT_VERSION "0.1.0"

static void usage(void)
{
    printf(
        "froot %s - FunTux multiroot tool (funobj object model)\n\n"
        "Usage: froot <command> [args]\n\n"
        "Commands:\n"
        "  list                        list configured roots\n"
        "  status <index>              show a root's state\n"
        "  mount <index>               mount virtual filesystems into a root\n"
        "  umount <index>              unmount a root's virtual filesystems\n"
        "  register <index> [path]     register a root with the funroot module\n"
        "  switch <index>              switch this process's / to a root (kernel)\n"
        "  enter <index> [cmd]         enter a root (kernel switch, or chroot)\n"
        "  strata                      list Bedrock Linux strata\n"
        "  bedrock-sync                map each Bedrock stratum to a root index\n"
        "  -h, --help                  show this help\n"
        "  -V, --version               show version\n\n"
        "Roots live under %s/<index> (override with $MROOT_ROOTS_DIR); index 0 is\n"
        "the host root (/). Under Bedrock Linux, bedrock-sync maps each\n"
        "/bedrock/strata/<name> to a root index, so switching a process's root is\n"
        "the same as switching its Bedrock stratum.\n",
        FROOT_VERSION, fun_root_base());
}

static unsigned parse_index(const char *s)
{
    char *end;
    long v;

    if (!s || !*s)
        return (unsigned)-1;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || v > FUNROOT_MAX_INDEX)
        return (unsigned)-1;
    return (unsigned)v;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }
    const char *cmd = argv[1];

    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
        usage();
        return 0;
    }
    if (!strcmp(cmd, "-V") || !strcmp(cmd, "--version")) {
        printf("froot %s\n", FROOT_VERSION);
        return 0;
    }

    if (!strcmp(cmd, "list")) {
        FunObject *r = fun_root_new(0);
        fun_send0(r, "list");
        free(r);
        return 0;
    }
    if (!strcmp(cmd, "strata")) {
        FunObject *r = fun_root_new(0);
        fun_send0(r, "strata");
        free(r);
        return 0;
    }
    if (!strcmp(cmd, "bedrock-sync")) {
        FunObject *r = fun_root_new(0);
        fun_send0(r, "bedrock-sync");
        free(r);
        return 0;
    }

    if (argc < 3) {
        fprintf(stderr, "froot: %s requires an index\n", cmd);
        return 2;
    }
    unsigned idx = parse_index(argv[2]);
    if (idx == (unsigned)-1) {
        fprintf(stderr, "froot: invalid index '%s'\n", argv[2]);
        return 2;
    }

    if (!strcmp(cmd, "status") || !strcmp(cmd, "mount") ||
        !strcmp(cmd, "umount") || !strcmp(cmd, "switch")) {
        FunObject *r = fun_root_new(idx);
        if (!strcmp(cmd, "status"))
            fun_send0(r, "status");
        else if (!strcmp(cmd, "mount"))
            fun_send0(r, "mount");
        else if (!strcmp(cmd, "umount"))
            fun_send0(r, "umount");
        else
            fun_send0(r, "switch");
        free(r);
        return 0;
    }

    if (!strcmp(cmd, "register")) {
        FunObject *r = fun_root_new(idx);
        if (argc >= 4)
            fun_send1(r, "register", (FunValue)argv[3]);
        else
            fun_send0(r, "register");
        free(r);
        return 0;
    }

    if (!strcmp(cmd, "enter")) {
        FunObject *r = fun_root_new(idx);
        if (argc >= 4)
            fun_send1(r, "enter", (FunValue)argv[3]);
        else
            fun_send0(r, "enter");
        free(r);
        return 0;
    }

    fprintf(stderr, "froot: unknown command '%s'\n", cmd);
    usage();
    return 2;
}
