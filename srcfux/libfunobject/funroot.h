/*
 * funroot.h - FunTux multiroot object model built on libfunobject.
 *
 * The multiroot philosophy expressed as objects:
 *
 *   FunMount   - one virtual filesystem (proc, sys, devpts, tmp, ...) that
 *                can be mounted into a root's tree.
 *   FunRoot    - one Linux root filesystem among several. index 0 is the
 *                host root ("/"), the rest live under $MROOT_ROOTS_DIR
 *                (default /var/roots/<index>).
 *   FunStratum - a Bedrock Linux stratum under /bedrock/strata/<name>,
 *                which is also a root (a FunRoot subclass).
 *
 * A root can be registered with the funroot kernel module (/dev/funroot) and
 * switched into with a single ioctl, making multiple roots a kernel-enforced
 * reality: switch a process's "/" and you have switched it into another root
 * filesystem (or, under Bedrock, another stratum).
 */
#ifndef FUNROOT_H
#define FUNROOT_H

#include "funobj.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FUNROOT_MAX_INDEX 255

/* Virtual filesystem mount. Fields are borrowed, not copied. */
FunObject *fun_mount_new(const char *source, const char *target,
                         const char *fstype, unsigned long flags,
                         const char *data);

/* Root with the given index. 0 is the host root. */
FunObject *fun_root_new(unsigned index);

/* Root with an explicit directory, bypassing the default /var/roots/<i>. */
FunObject *fun_root_new_path(unsigned index, const char *path);

/* Bedrock stratum object. Pass NULL/"" for the "any stratum" instance used
 * by the list/sync operations. */
FunObject *fun_stratum_new(const char *name);

/* The root base directory (honors $MROOT_ROOTS_DIR, like mroot). */
const char *fun_root_base(void);

#ifdef __cplusplus
}
#endif

#endif /* FUNROOT_H */
