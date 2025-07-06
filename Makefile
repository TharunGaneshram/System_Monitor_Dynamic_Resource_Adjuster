# KERNELDIR points to the directory containing the kernel source tree for your running kernel.
# On Ubuntu, this is usually found under /lib/modules/$(shell uname -r)/build
KERNELDIR ?= /lib/modules/$(shell uname -r)/build

# PWD points to the current directory where the Makefile is located.
PWD := $(shell pwd)

# obj-m specifies the object files that will be compiled into loadable modules.
# For your auto_health_monitor.c, you'd have obj-m := auto_health_monitor.o
obj-m := auto_health_monitor.o

# The 'all' target is the default. It uses the kernel build system's 'modules' target.
all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

# The 'clean' target removes generated files.
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean