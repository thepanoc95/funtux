// SPDX-License-Identifier: GPL-2.0-only
/*
 * funroot - FunTux multi-root namespace switcher.
 *
 * Each process has a "current root" among several registered roots; an
 * ioctl on /dev/funroot changes it, no re-exec or re-login needed. The
 * switch is kernel-enforced, like chroot() but switchable at runtime.
 * index 0 is the host root ("/"); the rest are $MROOT_ROOTS_DIR/<index>
 * (or a Bedrock stratum under /bedrock/strata/<name>).
 *
 * Interface (see uapi/funroot.h): ADD, DEL, SET, GET, PATH, COUNT.
 */

#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/user_namespace.h>

#include "funroot.h"

#define FUNROOT_VERSION "0.2.0"
#define DRIVER_NAME "funroot"

struct funroot_slot {
	bool used;
	char *path;		/* registered pathname, for reporting */
	struct path p;		/* pinned dentry + mount */
};

static struct funroot_slot funroot_slots[FUNROOT_MAX_ROOTS];
static DEFINE_MUTEX(funroot_lock);

static bool funroot_debug;
static bool funroot_user_switch = true;
static bool funroot_host_switch;

module_param(funroot_debug, bool, 0644);
module_param(funroot_user_switch, bool, 0644);
module_param(funroot_host_switch, bool, 0644);
MODULE_PARM_DESC(funroot_debug,
		 "verbose logging");
MODULE_PARM_DESC(funroot_user_switch,
		 "allow unprivileged processes to switch their own root");
MODULE_PARM_DESC(funroot_host_switch,
		 "allow unprivileged processes to switch back to root 0");

#define funroot_log(fmt, ...) \
	pr_info("funroot: " fmt "\n", ##__VA_ARGS__)
#define funroot_dbg(fmt, ...) \
	do { if (funroot_debug) pr_info("funroot: " fmt "\n", ##__VA_ARGS__); } while (0)

/* caller's current root, ref'd under fs->lock */
static struct path funroot_current_root(void)
{
	struct fs_struct *fs = current->fs;
	struct path p = {};

	if (!fs)
		return p;
	spin_lock(&fs->lock);
	p = fs->root;
	path_get(&p);
	spin_unlock(&fs->lock);
	return p;
}

static int funroot_add(const struct funroot_req __user *arg)
{
	struct funroot_req req;
	struct path p;
	char *name;
	int err;

	if (!ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	err = copy_from_user(&req, arg, sizeof(req));
	if (err)
		return -EFAULT;

	if (req.index >= FUNROOT_MAX_ROOTS) {
		funroot_dbg("add: index %u out of range", req.index);
		return -EINVAL;
	}
	req.path[FUNROOT_PATH_MAX - 1] = '\0';
	if (req.path[0] != '/') {
		funroot_dbg("add: root path must be absolute");
		return -EINVAL;
	}

	err = kern_path(req.path, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &p);
	if (err)
		return err;

	mutex_lock(&funroot_lock);
	if (funroot_slots[req.index].used) {
		err = -EBUSY;
	} else {
		name = kstrdup(req.path, GFP_KERNEL);
		if (!name) {
			err = -ENOMEM;
		} else {
			funroot_slots[req.index].path = name;
			funroot_slots[req.index].p = p;	/* take kern_path refs */
			funroot_slots[req.index].used = true;
			err = 0;
			funroot_log("root %u registered at %s", req.index, name);
		}
	}
	mutex_unlock(&funroot_lock);

	if (err)
		path_put(&p);
	return err;
}

static int funroot_del(const struct funroot_req __user *arg)
{
	struct funroot_req req;
	struct funroot_slot *s;
	int err;

	if (!ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	err = copy_from_user(&req, arg, sizeof(req));
	if (err)
		return -EFAULT;
	if (req.index >= FUNROOT_MAX_ROOTS)
		return -EINVAL;

	mutex_lock(&funroot_lock);
	s = &funroot_slots[req.index];
	if (!s->used) {
		err = -ENOENT;
	} else {
		path_put(&s->p);
		kfree(s->path);
		memset(s, 0, sizeof(*s));
		funroot_log("root %u unregistered", req.index);
		err = 0;
	}
	mutex_unlock(&funroot_lock);
	return err;
}

/* caller holds funroot_lock */
static int funroot_set_locked(unsigned index)
{
	struct funroot_slot *s;
	struct path newroot;
	struct fs_struct *fs = current->fs;
	int err;

	if (!fs)
		return -EINVAL;

	if (index == 0) {
		if (funroot_slots[0].used) {
			newroot = funroot_slots[0].p;
			path_get(&newroot);
		} else {
			err = kern_path("/", LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
					&newroot);
			if (err)
				return err;
		}
	} else {
		if (index >= FUNROOT_MAX_ROOTS || !funroot_slots[index].used)
			return -ENOENT;
		newroot = funroot_slots[index].p;
		path_get(&newroot);
	}

	if (!d_is_dir(newroot.dentry)) {
		path_put(&newroot);
		return -ENOTDIR;
	}

	/*
	 * set_fs_root()/set_fs_pwd() take their own ref and drop the old
	 * root, so our ref is released here. Affects every thread sharing
	 * this fs_struct, same as chroot().
	 */
	set_fs_root(fs, &newroot);
	set_fs_pwd(fs, &newroot);
	path_put(&newroot);
	funroot_dbg("pid %d switched root to %u", current->pid, index);
	return 0;
}

static int funroot_set(const struct funroot_req __user *arg)
{
	struct funroot_req req;
	int err;

	err = copy_from_user(&req, arg, sizeof(req));
	if (err)
		return -EFAULT;
	if (req.index >= FUNROOT_MAX_ROOTS)
		return -EINVAL;

	if (req.index == 0) {
		if (!ns_capable(current_user_ns(), CAP_SYS_ADMIN) &&
		    !funroot_host_switch)
			return -EPERM;
	} else if (!ns_capable(current_user_ns(), CAP_SYS_ADMIN) &&
		   !funroot_user_switch) {
		return -EPERM;
	}

	mutex_lock(&funroot_lock);
	err = funroot_set_locked(req.index);
	mutex_unlock(&funroot_lock);
	return err;
}

static int funroot_get(const struct funroot_req __user *arg)
{
	struct funroot_req req = { .index = 0 };
	struct path cur;
	unsigned int i;

	cur = funroot_current_root();
	mutex_lock(&funroot_lock);
	for (i = 0; i < FUNROOT_MAX_ROOTS; i++) {
		struct funroot_slot *s = &funroot_slots[i];

		if (!s->used)
			continue;
		if (path_equal(&cur, &s->p)) {
			req.index = i;
			strscpy(req.path, s->path, sizeof(req.path));
			break;
		}
	}
	mutex_unlock(&funroot_lock);
	path_put(&cur);

	if (copy_to_user(arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static int funroot_path(const struct funroot_req __user *arg)
{
	struct funroot_req req;
	int err;

	err = copy_from_user(&req, arg, sizeof(req));
	if (err)
		return -EFAULT;
	if (req.index >= FUNROOT_MAX_ROOTS)
		return -EINVAL;

	mutex_lock(&funroot_lock);
	if (!funroot_slots[req.index].used) {
		err = -ENOENT;
	} else {
		strscpy(req.path, funroot_slots[req.index].path,
			sizeof(req.path));
		err = 0;
	}
	mutex_unlock(&funroot_lock);
	if (err)
		return err;
	if (copy_to_user(arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static int funroot_count(const struct funroot_req __user *arg)
{
	struct funroot_req req = { .index = 0 };
	unsigned int i, n = 0;

	mutex_lock(&funroot_lock);
	for (i = 0; i < FUNROOT_MAX_ROOTS; i++)
		if (funroot_slots[i].used)
			n++;
	mutex_unlock(&funroot_lock);

	req.index = n;
	if (copy_to_user(arg, &req, sizeof(req)))
		return -EFAULT;
	return 0;
}

static long funroot_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case FUNROOT_ADD:
		return funroot_add(uarg);
	case FUNROOT_DEL:
		return funroot_del(uarg);
	case FUNROOT_SET:
		return funroot_set(uarg);
	case FUNROOT_GET:
		return funroot_get(uarg);
	case FUNROOT_PATH:
		return funroot_path(uarg);
	case FUNROOT_COUNT:
		return funroot_count(uarg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations funroot_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= funroot_ioctl,
	.compat_ioctl	= funroot_ioctl,
};

static int funroot_proc_show(struct seq_file *m, void *v)
{
	unsigned int i;

	mutex_lock(&funroot_lock);
	seq_puts(m, "index\tpath\n");
	for (i = 0; i < FUNROOT_MAX_ROOTS; i++) {
		struct funroot_slot *s = &funroot_slots[i];

		if (s->used)
			seq_printf(m, "%u\t%s\n", i, s->path);
	}
	mutex_unlock(&funroot_lock);
	return 0;
}

static int funroot_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, funroot_proc_show, NULL);
}

static const struct proc_ops funroot_proc_fops = {
	.proc_open	= funroot_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static struct miscdevice funroot_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DRIVER_NAME,
	.mode	= 0666,
	.fops	= &funroot_fops,
};

/*
 * Pin "/" at load so index 0 always means the boot-time root, even for
 * processes that have already switched away.
 */
static int __init funroot_register_host(void)
{
	struct path p;
	int err;

	err = kern_path("/", LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &p);
	if (err) {
		funroot_log("could not pin host root: %d", err);
		return err;
	}

	mutex_lock(&funroot_lock);
	if (!funroot_slots[0].used) {
		funroot_slots[0].path = kstrdup("/", GFP_KERNEL);
		if (!funroot_slots[0].path) {
			mutex_unlock(&funroot_lock);
			path_put(&p);
			return -ENOMEM;
		}
		funroot_slots[0].p = p;
		funroot_slots[0].used = true;
		funroot_log("host root pinned at index 0");
	} else {
		path_put(&p);
	}
	mutex_unlock(&funroot_lock);
	return 0;
}

static int __init funroot_init(void)
{
	int err;

	memset(funroot_slots, 0, sizeof(funroot_slots));

	err = misc_register(&funroot_misc);
	if (err)
		return err;

	err = funroot_register_host();
	if (err)
		return err;

	proc_create(DRIVER_NAME, 0444, NULL, &funroot_proc_fops);

	funroot_log("loaded v%s (%u root slots; user_switch=%d host_switch=%d)",
		    FUNROOT_VERSION, FUNROOT_MAX_ROOTS,
		    funroot_user_switch, funroot_host_switch);
	return 0;
}

static void __exit funroot_exit(void)
{
	unsigned int i;

	remove_proc_entry(DRIVER_NAME, NULL);
	misc_deregister(&funroot_misc);

	mutex_lock(&funroot_lock);
	for (i = 0; i < FUNROOT_MAX_ROOTS; i++) {
		struct funroot_slot *s = &funroot_slots[i];

		if (s->used) {
			path_put(&s->p);
			kfree(s->path);
			s->used = false;
		}
	}
	mutex_unlock(&funroot_lock);
}

module_init(funroot_init);
module_exit(funroot_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("FunTux");
MODULE_DESCRIPTION("FunTux multi-root namespace switcher");
MODULE_VERSION(FUNROOT_VERSION);
