# Out-of-tree Kbuild for the funroot kernel module.
#
#   make            build the module (needs kernel headers: make-kpkg, linux-headers)
#   make KVER=x.y   build against a specific kernel version
#   make install    install into the running kernel's module tree
#
obj-m := funroot.o

ccflags-y += -I$(src)/uapi

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build

.PHONY: all clean install

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

install: all
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install
