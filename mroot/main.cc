#define _BSD_SOURCE
#define _MROOT_VERSION "0.1.0"

#include <iostream>
#include <string>
#include <unistd.h>

#if defined(__LINUX__)
    #include <sys/syscall.h>
    #include <sys/mount.h>
    #include <sched.h>
#endif

