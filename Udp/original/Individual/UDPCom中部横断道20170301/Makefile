#ROOTDIR=/home/atmark/atmark-dist
ifndef ROOTDIR
ROOTDIR ?= ../atmark-dist
endif
ROMFSDIR = $(ROOTDIR)/romfs
ROMFSINST = romfs-inst.sh
PATH := $(PATH):$(ROOTDIR)/tools

UCLINUX_BUILD_USER = 1
include $(ROOTDIR)/.config
LIBCDIR = $(CONFIG_LIBCDIR)
include $(ROOTDIR)/config.arch

EXEC = udp
OBJS = MainLoop.o

all: $(EXEC)
$(EXEC): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS) -lrt

clean:
	-rm -f $(EXEC) *.elf *.gdb *.o *.^

romfs:
	$(ROMFSINST) /home/spinach/$(EXEC)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

