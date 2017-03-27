
CC = i586-poky-linux-gcc
ARCH = x86
CROSS_COMPILE = i586-poky-linux-
SROOT=/opt/iot-devkit/1.7.3/sysroots/i586-poky-linux/

KERNELDIR := /lib/modules/$(shell uname -r)/build
CLEANFILE := *.dis *.o *.ko *.mod.* *.symvers *.*.old *.order *.cmd

default:
	i586-poky-linux-gcc main.c -o main -Wall -lpthread --sysroot=$(SROOT)
	
clean:
	rm main
	