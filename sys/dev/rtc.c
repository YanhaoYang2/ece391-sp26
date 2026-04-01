// rtc.c - Goldfish RTC driver
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef RTC_TRACE
#define TRACE
#endif

#ifdef RTC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "misc.h"
#include "device.h"
#include "console.h"
#include "string.h"
#include "heap.h"
#include "ioimpl.h"

#include "error.h"

#include <stdint.h>

// INTERNAL TYPE DEFINITIONS
// 

struct rtc_regs {
    uint32_t time_low;  // read first, latches time_high
    uint32_t time_high; //
};

struct rtc_device {
    volatile struct rtc_regs * regs;
    struct io io;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int rtc_open(struct io ** ioptr, void * aux);
static void rtc_reclaim(struct io * io);
static long rtc_read(struct io * io, void * buf, long bufsz);

static uint64_t read_real_time(volatile struct rtc_regs * regs);

// INTERNAL GLOBAL VARIABLES AND CONSTANTS
//

static const struct iointf rtc_intf = {
    .implname = "rtc",
    .read = &rtc_read
};

// EXPORTED FUNCTION DEFINITIONS
// 

void attach_rtc(void * mmio_base) {
    // YOUR CODE HERE
    struct rtc_device * a = kmalloc(sizeof(*a));
    a->regs = (volatile struct rtc_regs *)mmio_base;
    ioinit(&a->io, &rtc_intf, 1, 0);
    register_device("rtc", -1, rtc_open, a);
}

int rtc_open(struct io ** ioptr, void * aux) {
    // YOUR CODE HERE
    struct rtc_device * a = aux;
    *ioptr = ioaddref(&a->io);
    return 0;
}

long rtc_read(struct io * io, void * buf, long bufsz) {
    // YOUR CODE HERE
    if(bufsz < 8){
        return -EINVAL;
    }
    struct rtc_device * a = (struct rtc_device *)((char *)io - offsetof(struct rtc_device, io));
    uint64_t b = read_real_time(a->regs);
    memcpy(buf, &b, 8);
    return 8;
}

uint64_t read_real_time(volatile struct rtc_regs * regs) {
    // YOUR CODE HERE
    uint32_t low = regs->time_low;
    uint32_t high = regs->time_high;
    return ((uint64_t)high << 32) | low;
}