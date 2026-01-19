# /*! @file Makefile
#     @brief Well it's a Makefile
#     @copyright Copyright (c) 2024-2026 University of Illinois
#     @license SPDX-License-identifier: NCSA
# */

PREFIX=riscv64-unknown-elf-
QEMU=qemu-system-riscv64

CC=$(PREFIX)gcc
AS=$(PREFIX)as
LD=$(PREFIX)ld

OBJS = \
	mp1.o \
	start.o \

CFLAGS = -Wall -Werror=implicit-function-declaration
CFLAGS += -fno-omit-frame-pointer -ggdb3 -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64imazicsr -mabi=lp64
CFLAGS += -fno-common -nostdlib -mno-relax -ffreestanding
CFLAGS += -fno-asynchronous-unwind-tables -mno-riscv-attribute
CFLAGS += -I.

ASFLAGS = -march=rv64imazicsr
LDFLAGS = -melf64lriscv

QEMUOPTS = -global virtio-mmio.force-legacy=false
QEMUOPTS += -machine virt -bios none

QEMUOPTS += -serial mon:stdio
QEMUOPTS += -object rng-random,filename=/dev/urandom,id=rng0
QEMUOPTS += -device virtio-rng-device,rng=rng0

QEMUOPTS += -device virtio-gpu-device
QEMUOPTS += -display gtk # For Linux
# QEMUOPTS += -display cocoa # For MacOS

###########################################################################

all: demo-kernel.elf

clean:
	rm -rf mp1.o start.o *.elf tests/*.o

###################################DEMO.O###################################

demo-kernel.elf: $(OBJS) demo.o kernel.a
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

run-demo: demo-kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-demo: demo-kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

###################################GOLD.O###################################

gold-kernel.elf: $(OBJS) gold.o kernel.a
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

run-gold: gold-kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

##################################TEST_*.O##################################

# e.g:
# "make test_add_star_simple.elf" to compile
# "make run-test_add_star_simple" to run
# "make debug-test_add_star_simple" to debug (followed by "riscv64-unknown-elf-gdb test_add_star_simple.elf" in another terminal)

test_%.elf: $(OBJS) tests/test_%.o kernel.a
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

run-test_%: test_%.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug-test_%: test_%.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

############################################################################