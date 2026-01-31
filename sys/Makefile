# mp2.make - MP2 makefile
# 

PREFIX=riscv64-unknown-elf-
QEMU=qemu-system-riscv64

CC=$(PREFIX)gcc
AS=$(PREFIX)as
LD=$(PREFIX)ld
OBJCOPY=$(PREFIX)objcopy
OBJDUMP=$(PREFIX)objdump
UNIFDEF=unifdef

OBJS = \
	start.o \
	sbi.o \
	misc.o \
	console.o \
	board/qvirt.o \
	dev/uart.o \
	dev/rtc.o \
	dev/virtio.o \
	dev/viorng.o \
	games/rule30-mp2.o \
	games/trek-mp2.o \
	string.o \
	plic.o \
	string.o \
	error.o \
	thrasm.o \
	trap.o \
	mp2-main.o \
	mp2-thread.o \
	mp2-timer.o \
	mp2-device.o \
	mp2-intr.o \
	mp2-excp.o \
	mp2-heap.o \
	mp2-io.o 

CFLAGS = -Wall -Werror=implicit-function-declaration -Wno-unused-function
CFLAGS += -fno-omit-frame-pointer -ggdb3 -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64imazicsr -mabi=lp64
CFLAGS += -fno-common -nostdlib -mno-relax -ffreestanding 
CFLAGS += -fno-asynchronous-unwind-tables -mno-riscv-attribute
CFLAGS += -I.

# CFLAGS += -DDEBUG -DTRACE # Everything!
# CFLAGS += -DHEAP_DEBUG -DHEAP_TRACE
# CFLAGS += -DMAIN_DEBUG -DMAIN_TRACE
# CFLAGS += -DTHREAD_DEBUG -DTHREAD_TRACE
# CFLAGS += -DTIMER_DEBUG -DTIMER_TRACE

ASFLAGS = -march=rv64imazicsr -defsym MP2=1

LDFLAGS = -melf64lriscv -T board/qvirt.ld

QEMUOPTS = -global virtio-mmio.force-legacy=false
QEMUOPTS += -machine virt -nographic
QEMUOPTS += -object rng-random,filename=/dev/urandom,id=rng0
QEMUOPTS += -device virtio-rng-device,rng=rng0
QEMUOPTS += -bios bios/osbi181q.bin
QEMUOPTS += -serial mon:stdio
QEMUOPTS += -serial pty
QEMUOPTS += -serial pty

all: kernel.elf

clean:
	rm -rf board/*.o dev/*.o fs/*.o *.o *.elf

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

run: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $<

debug: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 8M -kernel $< -S -s

mp2-%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) -DMP2 $<

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<
