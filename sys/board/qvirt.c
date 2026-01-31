// qemu-virt.c - QEMU virt system initialization
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include <stddef.h>

#include "console.h" // console_init();
#include "heap.h" // heap_init()

// Run-time QEMU configuration
//

#define NUART 3
#define RAM_SIZE_MB (8-2)

// QEMU constants
//

#define RAM_START_PMA 0x80000000UL

#ifndef TIMER_FREQ
#define TIMER_FREQ 10000000UL  // qemu/include/hw/intc/riscv_aclint.h
#endif

// MMIO addresses
//

#ifndef PLIC_MMIO_BASE
#define PLIC_MMIO_BASE 0x0C000000L
#endif

#define UART0_MMIO_BASE 0x10000000UL  // PMA
#define UART1_MMIO_BASE 0x10000100UL  // PMA
#define UART_MMIO_BASE(i) (UART0_MMIO_BASE + (i) * (UART1_MMIO_BASE - UART0_MMIO_BASE))
#define UART0_INTR_SRCNO 10

#define VIRTIO0_MMIO_BASE 0x10001000UL  // PMA
#define VIRTIO1_MMIO_BASE 0x10002000UL  // PMA
#define VIRTIO_MMIO_BASE(i) (VIRTIO0_MMIO_BASE + (i) * (VIRTIO1_MMIO_BASE - VIRTIO0_MMIO_BASE))
#define VIRTIO0_INTR_SRCNO 1

#define RTC_MMIO_BASE 0x00101000L

// Additional derived constants
//

#define RAM_SIZE ((size_t)RAM_SIZE_MB * 1024 * 1024)
#define RAM_START ((void*)RAM_START_PMA)
#define RAM_END_PMA (RAM_START_PMA + RAM_SIZE)
#define RAM_END (RAM_START + RAM_SIZE)

// Device attach function declarations
//

extern void plic_init(void * mmio_base); // plic.c
extern void timer_init(unsigned int freq); // timer.c

extern void attach_uart(void * mmio_base, int irqno); // dev/uart.c
extern void attach_virtio(void * mmio_base, int irqno); // dev/virtio.c
extern void attach_rtc(void * mmio_base); // dev/rtc.c

void board_init(unsigned int hartid, void * dtb) {    
    console_init();
    plic_init((void*)PLIC_MMIO_BASE);
    timer_init(TIMER_FREQ);

    extern char _kimg_end[]; // from kernel.ld
    heap_init(_kimg_end, RAM_END - (void*)_kimg_end);
}

void attach_devices(void) {
    int i;

    attach_rtc((void*)RTC_MMIO_BASE);

    for (i = 0; i < NUART; i++)
        attach_uart((void*)UART_MMIO_BASE(i), UART0_INTR_SRCNO+i);
    
    for (i = 0; i < 8; i++)
        attach_virtio((void*)VIRTIO_MMIO_BASE(i), VIRTIO0_INTR_SRCNO+i);
}