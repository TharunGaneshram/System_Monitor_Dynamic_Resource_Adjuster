KERNELDIR ?= /lib/modules/$(shell uname -r)/build

PWD := $(shell pwd)

obj-m := auto_health_monitor.o

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean