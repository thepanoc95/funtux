#define _BSD_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <funroot.h>

#define FUNROOT_VERSION "0.1.0"
#define FUNROOT_DEV "/dev/funroot"

static int dev_open(void)
{
	int fd = open(FUNROOT_DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		fprintf(stderr, "funroot: cannot open %s: %s\n",
			FUNROOT_DEV, strerror(errno));
	return fd;
}

static int parse_index(const char *s)
{
	char *end;
	long v;

	if (!s || !*s)
		return -1;
	errno = 0;
	v = strtol(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0')
		return -1;
	if (v < 0 || v >= FUNROOT_MAX_ROOTS)
		return -1;
	return (int)v;
}

static int cmd_add(const char *index_str, const char *path)
{
	struct funroot_req req;
	int fd, rc, index;

	index = parse_index(index_str);
	if (index < 0) {
		fprintf(stderr, "funroot: invalid index '%s'\n", index_str);
		return 2;
	}
	if (path[0] != '/') {
		fprintf(stderr, "funroot: root path must be absolute: %s\n", path);
		return 2;
	}
	memset(&req, 0, sizeof(req));
	req.index = (unsigned)index;
	strncpy(req.path, path, sizeof(req.path) - 1);

	fd = dev_open();
	if (fd < 0)
		return 1;
	rc = ioctl(fd, FUNROOT_ADD, &req);
	if (rc < 0 && errno != EBUSY) {
		fprintf(stderr, "funroot: add root %d: %s\n", index,
			strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	printf("funroot: root %d -> %s registered\n", index, path);
	return 0;
}

static int cmd_del(const char *index_str)
{
	struct funroot_req req;
	int fd, index;

	index = parse_index(index_str);
	if (index < 0) {
		fprintf(stderr, "funroot: invalid index '%s'\n", index_str);
		return 2;
	}
	memset(&req, 0, sizeof(req));
	req.index = (unsigned)index;
	fd = dev_open();
	if (fd < 0)
		return 1;
	if (ioctl(fd, FUNROOT_DEL, &req) < 0) {
		fprintf(stderr, "funroot: del root %d: %s\n", index,
			strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	printf("funroot: root %d unregistered\n", index);
	return 0;
}

static int cmd_set(const char *index_str)
{
	struct funroot_req req;
	int fd, index;

	index = parse_index(index_str);
	if (index < 0) {
		fprintf(stderr, "funroot: invalid index '%s'\n", index_str);
		return 2;
	}
	memset(&req, 0, sizeof(req));
	req.index = (unsigned)index;
	fd = dev_open();
	if (fd < 0)
		return 1;
	if (ioctl(fd, FUNROOT_SET, &req) < 0) {
		fprintf(stderr, "funroot: switch to root %d: %s\n", index,
			strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

static int cmd_get(void)
{
	struct funroot_req req;
	int fd;

	memset(&req, 0, sizeof(req));
	fd = dev_open();
	if (fd < 0)
		return 1;
	if (ioctl(fd, FUNROOT_GET, &req) < 0) {
		fprintf(stderr, "funroot: get: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	printf("index: %u\n", req.index);
	if (req.path[0])
		printf("path:  %s\n", req.path);
	return 0;
}

static int cmd_list(void)
{
	unsigned int i;
	int fd;

	fd = dev_open();
	if (fd < 0)
		return 1;
	printf("index\tpath\n");
	for (i = 0; i < FUNROOT_MAX_ROOTS; i++) {
		struct funroot_req req;

		memset(&req, 0, sizeof(req));
		req.index = i;
		if (ioctl(fd, FUNROOT_PATH, &req) == 0)
			printf("%u\t%s\n", i, req.path);
	}
	close(fd);
	return 0;
}

static int cmd_enter(const char *index_str, int argc, char **argv)
{
	int index;

	index = parse_index(index_str);
	if (index < 0) {
		fprintf(stderr, "funroot: invalid index '%s'\n", index_str);
		return 2;
	}
	if (cmd_set(index_str) != 0)
		return 1;
	if (chdir("/") != 0) {
		fprintf(stderr, "funroot: chdir(/): %s\n", strerror(errno));
		return 1;
	}
	if (argc == 0) {
		char *sh[] = { "/bin/sh", NULL };
		execv(sh[0], sh);
	} else {
		execvp(argv[0], argv);
	}
	fprintf(stderr, "funroot: exec: %s\n", strerror(errno));
	return 127;
}

static void usage(void)
{
	printf(
		"funroot %s - FunTux multi-root namespace switcher (kernel module client)\n\n"
		"Usage: funroot <command> [args]\n\n"
		"Commands:\n"
		"  add <index> <path>   register a root directory at <index>\n"
		"  del <index>          unregister a root\n"
		"  set <index>          switch this process's / to root <index>\n"
		"  enter <index> [cmd]  switch root, then exec cmd (default /bin/sh)\n"
		"  host                 switch back to the host root (index 0)\n"
		"  get                  show this process's current root\n"
		"  list                 list registered roots\n"
		"  -h, --help           show this help\n"
		"  -V, --version        show version\n",
		FUNROOT_VERSION);
}

int main(int argc, char **argv)
{
	const char *cmd;

	if (argc < 2) {
		usage();
		return 2;
	}
	cmd = argv[1];

	if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
		usage();
		return 0;
	}
	if (!strcmp(cmd, "-V") || !strcmp(cmd, "--version")) {
		printf("funroot %s\n", FUNROOT_VERSION);
		return 0;
	}
	if (!strcmp(cmd, "add") && argc == 4)
		return cmd_add(argv[2], argv[3]);
	if (!strcmp(cmd, "del") && argc == 3)
		return cmd_del(argv[2]);
	if (!strcmp(cmd, "set") && argc == 3)
		return cmd_set(argv[2]);
	if (!strcmp(cmd, "host"))
		return cmd_set("0");
	if (!strcmp(cmd, "enter")) {
		if (argc < 3) {
			fprintf(stderr, "funroot: enter requires an index\n");
			return 2;
		}
		return cmd_enter(argv[2], argc - 3, argc > 3 ? &argv[3] : NULL);
	}
	if (!strcmp(cmd, "get"))
		return cmd_get();
	if (!strcmp(cmd, "list"))
		return cmd_list();

	fprintf(stderr, "funroot: unknown or incomplete command '%s'\n", cmd);
	usage();
	return 2;
}
