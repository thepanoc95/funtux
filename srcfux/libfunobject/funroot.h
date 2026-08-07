/*
 * funroot.h - the FunTux root object model. index 0 is the host root,
 * the rest live under $MROOT_ROOTS_DIR (default /var/roots/<index>).
 * Under Bedrock, a root can also be a /bedrock/strata/<name>.
 */
#ifndef FUNROOT_H
#define FUNROOT_H

#include "funobj.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FUNROOT_MAX_INDEX 255

FunObject *fun_mount_new(const char *source, const char *target,
                         const char *fstype, unsigned long flags,
                         const char *data);
FunObject *fun_root_new(unsigned index);
FunObject *fun_root_new_path(unsigned index, const char *path);
FunObject *fun_stratum_new(const char *name);
const char *fun_root_base(void);

#ifdef __cplusplus
}
#endif

#endif /* FUNROOT_H */
