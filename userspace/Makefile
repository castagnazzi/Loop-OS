# Userspace build: ulib (the minimal C library) and the programs in bin/.
#
#   make            build everything into ../build/userspace
#   make install    copy the programs into $(ROOTFS) (used by `make initrd`)
#
# The previous, never-built scaffold lives in legacy/.

TARGET   := x86_64-elf
CC       := $(TARGET)-gcc
LD       := $(TARGET)-ld
AR       := $(TARGET)-ar

BUILD    := ../build/userspace
ROOTFS   ?= ../build/rootfs

# Freestanding user code: no SSE (the kernel does not save FPU state yet),
# no PIC (programs are linked at a fixed address), and no loop-to-memset
# rewriting inside our own memset/memcpy.
CFLAGS   := -std=gnu11 -O2 -g -Wall -Wextra \
            -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
            -fno-tree-loop-distribute-patterns \
            -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-3dnow \
            -Iinclude -I../kernel/include

LDFLAGS  := -nostdlib -static -z max-page-size=0x1000 -T user.ld

PROGS    := init sh hello ls cat sleep ps kill spin crash
BINS     := $(addprefix $(BUILD)/bin/,$(PROGS))
LIB      := $(BUILD)/libulib.a
CRT0     := $(BUILD)/crt0.o

.PHONY: all install clean
all: $(BINS)

$(CRT0): lib/crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ulib.o: lib/ulib.c include/ulib.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(BUILD)/ulib.o
	$(AR) rcs $@ $^

$(BUILD)/obj/%.o: bin/%.c include/ulib.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/bin/%: $(BUILD)/obj/%.o $(CRT0) $(LIB) user.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $< $(LIB)

install: all
	@mkdir -p $(ROOTFS)/bin $(ROOTFS)/sbin
	@cp $(BUILD)/bin/init $(ROOTFS)/sbin/init
	@for p in $(filter-out init,$(PROGS)); do cp $(BUILD)/bin/$$p $(ROOTFS)/bin/$$p; done
	@echo "Userspace installed into $(ROOTFS)"

clean:
	rm -rf $(BUILD)
