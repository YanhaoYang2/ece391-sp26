// uart.c - NS8550-compatible UART
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef UART_TRACE
#define TRACE
#endif

#ifdef UART_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "intr.h"
#include "heap.h"
#include "thread.h"
#include "console.h"
#include "device.h"
#include "misc.h"
#include "ioimpl.h"
#include "error.h"

#include <stdint.h>


// COMPILE-TIME CONSTANT DEFINITIONS
//

#ifndef UART_RBUFSZ
#define UART_RBUFSZ 64
#endif

#ifndef UART_INTR_PRIO
#define UART_INTR_PRIO 1
#endif

#ifndef UART_DEVNAME
#define UART_DEVNAME "uart"
#endif


// INTERNAL TYPE DEFINITIONS
// 

struct uart_regs {
    union {
        char rbr; // DLAB=0 read
        char thr; // DLAB=0 write
        uint8_t dll; // DLAB=1
    };
    
    union {
        uint8_t ier; // DLAB=0
        uint8_t dlm; // DLAB=1
    };
    
    union {
        uint8_t iir; // read
        uint8_t fcr; // write
    };

    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
};

#define LCR_DLAB (1 << 7)
#define LSR_OE (1 << 1)
#define LSR_DR (1 << 0)
#define LSR_THRE (1 << 5)
#define IER_DRIE (1 << 0)
#define IER_THREIE (1 << 1)

// Simple fixed-size ring buffer

struct ringbuf {
    unsigned int hpos; // head of queue (from where elements are removed)
    unsigned int tpos; // tail of queue (where elements are inserted)
    char data[UART_RBUFSZ];
};

// UART device structure

struct uart_device {
    volatile struct uart_regs * regs;
    int irqno;

    struct io io;

    // YOUR CODE HERE

    struct ringbuf rxbuf;
    struct ringbuf txbuf;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int uart_open(struct io ** ioptr, void * aux);
static void uart_reclaim(struct io * io);
static long uart_read(struct io * io, void * buf, long bufsz);
static long uart_write(struct io * io, const void * buf, long len);

static void uart_isr(int srcno, void * aux);

// Ring buffer (struct rbuf) functions

static void rbuf_init(struct ringbuf * rbuf);
static int rbuf_empty(const struct ringbuf * rbuf);
static int rbuf_full(const struct ringbuf * rbuf);
static void rbuf_putc(struct ringbuf * rbuf, char c);
static char rbuf_getc(struct ringbuf * rbuf);

// INTERNAL GLOBAL VARIABLES
//

static const struct iointf uart_intf = {
    .implname = "uart",
    .read = &uart_read,
    .write = &uart_write,
    .reclaim = &uart_reclaim
};

// EXPORTED FUNCTION DEFINITIONS
// 

void attach_uart(void * mmio_base, int irqno) {
    static unsigned short instcnt = 0; // number of UARTs
    struct uart_device * uart;

    uart = kcalloc(1, sizeof(*uart));

    uart->regs = mmio_base;
    uart->irqno = irqno;

    // YOUR CODE HERE

    // Initialize hardware device

    uart->regs->ier = 0;
    uart->regs->lcr = LCR_DLAB;
    // fence o,o ?
    uart->regs->dll = 0x01;
    uart->regs->dlm = 0x00;
    // fence o,o ?
    uart->regs->lcr = 0; // DLAB=0

    register_device(UART_DEVNAME, instcnt++, &uart_open, uart);
    ioinit(&uart->io, &uart_intf, 1, 0);
}

int uart_open(struct io ** ioptr, void * aux) {
    struct uart_device * const uart = aux;

    trace("%s()", __func__);

    if (iorefcnt(&uart->io) != 0)
        return -EBUSY;
    
    // Reset receive and transmit buffers
    
    rbuf_init(&uart->rxbuf);
    rbuf_init(&uart->txbuf);

    // Read RBR to flush any stale data in hardware buffer

    uart->regs->rbr; // forces a read because uart->regs is volatile

    // YOUR CODE HERE
    *ioptr = ioaddref(&uart->io);
    enable_intr_source(uart->irqno,1,uart_isr,uart);
    enable_interrupts();
    uart->regs->ier |= IER_DRIE;
    uart->regs->ier |= IER_THREIE;
    return 0;
}

void uart_reclaim(struct io * io) {
    struct uart_device * const uart =
        (void*)io - offsetof(struct uart_device, io);

    trace("%s()", __func__);
    // YOUR CODE HERE
    uart->regs->ier &= ~IER_DRIE;
    uart->regs->ier &= ~IER_THREIE;
    disable_intr_source(uart->irqno);

}

long uart_read(struct io * io, void * buf, long bufsz) {
    // YOUR CODE HERE
    if(bufsz==0) return 0;
    struct uart_device * const uart = (void*)io - offsetof(struct uart_device, io);
    long count=0;
    while(rbuf_empty(&uart->rxbuf)){
       continue;
       }
    
    char* store=buf;
    while(!rbuf_empty(&uart->rxbuf) && count<bufsz){
        store[count]=rbuf_getc(&uart->rxbuf);
        count++;
    }
    uart->regs->ier |= IER_DRIE; 
    return count;
}

long uart_write(struct io * io, const void * buf, long buflen) {
    // YOUR CODE HERE
    if(buflen==0) return 0;
     struct uart_device * const uart = (void*)io - offsetof(struct uart_device, io);
     long count=0;
    const char* write=buf;
     while(count<buflen){
         if(rbuf_full(&uart->txbuf)){
            continue;
        }
        rbuf_putc(&uart->txbuf,write[count]);
        count++;
        uart->regs->ier |= IER_THREIE; 
     }
     return count;
}

void uart_isr(int srcno, void * aux) {
    // YOUR CODE HERE
    struct uart_device * const uart = aux;
    char c;
    if(uart->regs->lsr&LSR_DR){
        c=uart->regs->rbr;
        if(!rbuf_full(&uart->rxbuf)){
         rbuf_putc(&uart->rxbuf,c);}else{
            uart->regs->ier &= ~IER_DRIE;
         }
    }
    if(uart->regs->lsr&LSR_THRE){
        if(!rbuf_empty(&uart->txbuf)){
        c=rbuf_getc(&uart->txbuf);
        uart->regs->thr=c;
    }   else{
        uart->regs->ier &= ~IER_THREIE; 
        }
    }

}

void rbuf_init(struct ringbuf * rbuf) {
    rbuf->hpos = 0;
    rbuf->tpos = 0;
}

int rbuf_empty(const struct ringbuf * rbuf) {
    return (rbuf->hpos == rbuf->tpos);
}

int rbuf_full(const struct ringbuf * rbuf) {
    return (rbuf->tpos - rbuf->hpos == UART_RBUFSZ);
}

void rbuf_putc(struct ringbuf * rbuf, char c) {
    uint_fast16_t tpos;

    tpos = rbuf->tpos;
    rbuf->data[tpos % UART_RBUFSZ] = c;
    asm volatile ("" ::: "memory");
    rbuf->tpos = tpos + 1;
}

char rbuf_getc(struct ringbuf * rbuf) {
    uint_fast16_t hpos;
    char c;

    hpos = rbuf->hpos;
    c = rbuf->data[hpos % UART_RBUFSZ];
    asm volatile ("" ::: "memory");
    rbuf->hpos = hpos + 1;
    return c;
}