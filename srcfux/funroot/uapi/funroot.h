/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * funroot UAPI - shared between the kernel module and userland
 * (mroot, funroot). index 0 = host root.
 */
#ifndef _UAPI_FUNROOT_H
#define _UAPI_FUNROOT_H

#include <stdint.h>
#include <sys/ioctl.h>

typedef uint32_t __u32;

#define FUNROOT_MAGIC		0xf0
#define FUNROOT_PATH_MAX	4096
#define FUNROOT_MAX_ROOTS	256

struct funroot_req {
	__u32 index;
	__u32 flags;		/* must be 0 */
	char path[FUNROOT_PATH_MAX];
};

#define FUNROOT_ADD		_IOW(FUNROOT_MAGIC, 1, struct funroot_req)
#define FUNROOT_DEL		_IOW(FUNROOT_MAGIC, 2, struct funroot_req)
#define FUNROOT_SET		_IOW(FUNROOT_MAGIC, 3, struct funroot_req)
#define FUNROOT_GET		_IOR(FUNROOT_MAGIC, 4, struct funroot_req)
#define FUNROOT_PATH		_IOWR(FUNROOT_MAGIC, 5, struct funroot_req)
#define FUNROOT_COUNT		_IOR(FUNROOT_MAGIC, 6, struct funroot_req)

#endif /* _UAPI_FUNROOT_H */
