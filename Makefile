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
OBJCOPY=$(PREFIX)objcopy
OBJDUMP=$(PREFIX)objdump

CFLAGS = -Wall -Werror=implicit-function-declaration
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64imazicsr -mabi=lp64
CFLAGS += -fno-common -nostdlib -mno-relax -ffreestanding 
CFLAGS += -fno-asynchronous-unwind-tables -mno-riscv-attribute
CFLAGS += -I.

ASFLAGS = -march=rv64imazicsr
LDFLAGS = -melf64lriscv -L.

QEMUOPTS = -machine virt -bios none -nographic
QEMUOPTS += -serial mon:stdio

###########################################################################

all: kernel.elf

clean:
	rm -rf *.elf start.o gdb_main.o

###################################MAIN.C##################################
# This command will run trek
kernel.elf: main.o start.o kernel.a
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

run: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

#################################GDB_MAIN.C################################
# specifically compile gdb_main.c using debugging flags
# this will be used for gdb practice

gdb_main.o: gdb_main.c
	$(CC) $(CFLAGS) -fno-omit-frame-pointer -ggdb3 -gdwarf -c -o $@ $<

gdb_main.elf: gdb_main.o start.o kernel.a
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $^

debug-gdb: gdb_main.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

###########################################################################