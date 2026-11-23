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
CFLAGS   := -std=gnu11 -O2 -g -Wall -Wextra -MMD -MP \
            -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
            -fno-tree-loop-distribute-patterns \
            -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-3dnow \
            -Iinclude -I../kernel/include

LDFLAGS  := -nostdlib -static -z max-page-size=0x1000 -T user.ld

PROGS    := init sh hello ls cat sleep ps kill spin crash mkdir rm cp wc term notepad files calc taskmgr about ping wget ifconfig nslookup rede teclado imagem navegador cobrinha velha
BINS     := $(addprefix $(BUILD)/bin/,$(PROGS))
LIB      := $(BUILD)/libulib.a
CRT0     := $(BUILD)/crt0.o

# TLS (BearSSL, vendored in tls/bearssl) is linked only into the programs that need it.
TLS_PROGS := wget navegador
TLS_BINS  := $(addprefix $(BUILD)/bin/,$(TLS_PROGS))
TLS_SRCS  := $(shell find tls/bearssl/src -name '*.c')
TLS_OBJS  := $(patsubst tls/bearssl/src/%.c,$(BUILD)/tls/%.o,$(TLS_SRCS)) $(BUILD)/tls/tls.o
TLSLIB    := $(BUILD)/libtls.a
# Soft-float/SSE-free build: no AES-NI/SSE2/RDRAND intrinsics and no OS entropy or clock sources
TLS_FLAGS := -w -Itls/shim -Itls/bearssl/src -Itls/bearssl/inc              -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_RDRAND=0 -DBR_POWER8=0

-include $(wildcard $(BUILD)/*.d $(BUILD)/obj/*.d $(BUILD)/tls/*.d)

.PHONY: all install clean
all: $(BINS)

$(CRT0): lib/crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ulib.o: lib/ulib.c include/ulib.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gui.o: lib/gui.c include/gui.h include/ulib.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(BUILD)/ulib.o $(BUILD)/gui.o
	$(AR) rcs $@ $^

$(BUILD)/obj/%.o: bin/%.c include/ulib.h include/gui.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/bin/%: $(BUILD)/obj/%.o $(CRT0) $(LIB) user.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $< $(LIB)

$(BUILD)/tls/tls.o: tls/tls.c tls/anchors.inc include/tls.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Itls/shim -c $< -o $@

$(BUILD)/tls/%.o: tls/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TLS_FLAGS) -c $< -o $@

$(TLSLIB): $(TLS_OBJS)
	$(AR) rcs $@ $^

$(TLS_BINS): $(BUILD)/bin/%: $(BUILD)/obj/%.o $(CRT0) $(LIB) $(TLSLIB) user.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CRT0) $< $(TLSLIB) $(LIB)

install: all
	@mkdir -p $(ROOTFS)/bin $(ROOTFS)/sbin
	@cp $(BUILD)/bin/init $(ROOTFS)/sbin/init
	@for p in $(filter-out init,$(PROGS)); do cp $(BUILD)/bin/$$p $(ROOTFS)/bin/$$p; done
	@echo "Userspace installed into $(ROOTFS)"

clean:
	rm -rf $(BUILD)
