// vioblk.c - VirtIO block device
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef VIOBLK_TRACE
#define TRACE
#endif

#ifdef VIOBLK_DEBUG
#define DEBUG
#endif

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "device.h"
#include "thread.h"
#include "error.h"
#include "string.h"
#include "conf.h"
#include "misc.h"
#include "error.h"
#include "console.h"
#include "ioimpl.h"

#include <limits.h>

// COMPILE-TIME PARAMETERS
//

#ifndef VIOBLK_INTR_PRIO
#define VIOBLK_INTR_PRIO 1
#endif

#ifndef VIOBLK_NAME
#define VIOBLK_NAME "vioblk"
#endif

// INTERNAL CONSTANT DEFINITIONS
//

// VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

// INTERNAL TYPE DEFINITIONS
//

// All VirtIO block device requests consist of a request header, defined below,
// followed by data, followed by a status byte. The header is device-read-only,
// the data may be device-read-only or device-written (depending on request
// type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

// Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

// Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

/**
 * @brief VirtIO Block Device with virtqueues and condition variables
 */ 

struct vioblk_device {
    // YOUR CODE HERE
    struct io io;

    volatile struct virtio_mmio_regs * regs;
    int irqno;
    unsigned int blksz;
    unsigned long long size;                //total disk capacity in bytes

    struct condition used_updated;
    struct rwlock lock;

    uint16_t qlen;                          //number of the descriptor spaces in the virtqueue
    uint16_t lui;               //last used index, allow increment

    int vq_flag;

    struct vioblk_request_header reqhdr;
    uint8_t status;

    struct {
        uint64_t addr;
        uint32_t len;
        uint16_t flags;
        uint16_t next;
    } desc[1];              //the descriptor placed in the available ring

    struct {
        uint64_t addr;
        uint32_t len;
        uint16_t flags;
        uint16_t next;
    } indirect_desc[3];     //request header, data buffer, status byte

    struct {
        uint16_t flags;
        uint16_t idx;
        uint16_t ring[1];
        uint16_t used_event;
    } avail;                //the available ring

    volatile struct {
        uint16_t flags;
        uint16_t idx;
        struct {
            uint32_t id;
            uint32_t len;
        } ring[1];
    } used;                 //the used ring
};

// INTERNAL FUNCTION DECLARATIONS
//

static int vioblk_open(struct io ** ioptr, void * aux);

static void vioblk_reclaim(struct io * io);

static long vioblk_fetch (
    struct io * io, unsigned long long pos, void * buf, long len);

static long vioblk_store (
    struct io * io, unsigned long long pos, const void * buf, long len);

static int vioblk_ioctl(struct io * io, int op, void * arg);

static void vioblk_isr(int srcno, void * aux);
        
        
// INTERNAL GLOBAL VARIABLES
//

static const struct iointf vioblk_intf = {
    .implname = "vioblk",
    .reclaim = &vioblk_reclaim,
    .fetch = &vioblk_fetch,
    .store = &vioblk_store,
    .ioctl = &vioblk_ioctl
};

// EXPORTED FUNCTION DEFINITIONS
//

// The vioblk_attach function is declared and called from virtio_attach() in
// virtio.c when a VirtIO block device is found.

void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    static unsigned short instcnt = 0; // number of vioblk devices
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * vb;
    unsigned int blksz;
    int result;
    
	trace("%s(regs=%p,irqno=%d)", __func__, regs, irqno);

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    // Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    __sync_synchronize(); // fence o,io

    // Negotiate features. We need:
    //  - VIRTIO_F_RING_RESET and
    //  - VIRTIO_F_INDIRECT_DESC
    // We want:
    //  - VIRTIO_BLK_F_BLK_SIZE and
    //  - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    // blksz must be a power of two
    assert (((blksz - 1) & blksz) == 0);

    // YOUR CODE HERE
    vb = kcalloc(1, sizeof(*vb));
    vb->regs = regs;
    vb->irqno = irqno;
    vb->blksz = blksz;
    vb->size = regs->config.blk.capacity * 512ULL;
    vb->qlen = 1;
    vb->lui = 0;
    condition_init(&vb->used_updated, "vioblk.used_updated");
    rwlock_init(&vb->lock, "vioblk.lock");
//allocate and initialize the device structure
    vb->desc[0].addr = (uint64_t)(uintptr_t)vb->indirect_desc;
    vb->desc[0].len = sizeof(vb->indirect_desc);
    vb->desc[0].flags = VIRTQ_DESC_F_INDIRECT;
    vb->desc[0].next = 0;

    vb->indirect_desc[0].addr = (uint64_t)(uintptr_t)&vb->reqhdr;
    vb->indirect_desc[0].len = sizeof(vb->reqhdr);
    vb->indirect_desc[0].flags = VIRTQ_DESC_F_NEXT;
    vb->indirect_desc[0].next = 1;

    vb->indirect_desc[2].addr = (uint64_t)(uintptr_t)&vb->status;
    vb->indirect_desc[2].len = 1;
    vb->indirect_desc[2].flags = VIRTQ_DESC_F_WRITE;
    vb->indirect_desc[2].next = 0;
//initialize synchronization, build the descriptor layout, and attach the virq to the device
    regs->queue_sel = 0;
    __sync_synchronize();
    //write descriptor table, aval, device
    virtio_attach_virtq(regs, 0, vb->qlen, (uint64_t)(uintptr_t)vb->desc, (uint64_t)(uintptr_t)&vb->used, (uint64_t)(uintptr_t)&vb->avail);

    ioinit(&vb->io, &vioblk_intf, blksz, 0);

    regs->status |= VIRTIO_STAT_DRIVER_OK;
    __sync_synchronize();

    register_device(VIOBLK_NAME, instcnt++, &vioblk_open, vb);
}

int vioblk_open(struct io ** ioptr, void * aux) {
    // YOUR CODE HERE
    struct vioblk_device * const vb = aux;
//handle firstly open initialization
    if (iorefcnt(&vb->io) == 0) {
        vb->regs->interrupt_ack = vb->regs->interrupt_status;
        __sync_synchronize();
        if (!vb->vq_flag) {
            virtio_enable_virtq(vb->regs, 0);
            vb->vq_flag = 1;
        }
//enable intterupts ad the virtq
        enable_intr_source(vb->irqno, VIOBLK_INTR_PRIO, vioblk_isr, vb);
        // virtio_enable_virtq(vb->regs, 0);
    }

    *ioptr = ioaddref(&vb->io);
    return 0;
}

void vioblk_reclaim(struct io * io) {
    // YOUR CODE HERE
    struct vioblk_device * const vb = (struct vioblk_device *)io;

    disable_intr_source(vb->irqno);     //disabled the interrupt source

    // vb->regs->queue_sel = 0;
    // __sync_synchronize();
    // vb->regs->queue_ready = 0;

    vb->regs->interrupt_ack = vb->regs->interrupt_status;   //clear the device's intterupt status
    __sync_synchronize();
}

long vioblk_fetch (
    struct io * io,
    unsigned long long bytepos,
    void * buf,
    long bytecnt)
{
    // YOUR CODE HERE
    struct vioblk_device * const vb = (struct vioblk_device *)io;
//check if the request is in the device boundary
    if (bytecnt == 0)
        return 0;

    if (vb->size < bytepos || vb->size - bytepos < (unsigned long long)bytecnt)
        return -EINVAL;
//serialize the request and build the block
    rwlock_acquire(&vb->lock, 1);

    vb->reqhdr.type = VIRTIO_BLK_T_IN;
    vb->reqhdr.reserved = 0;
    vb->reqhdr.sector = bytepos / 512;
    vb->status = 0xFF;

    vb->indirect_desc[1].addr = (uint64_t)(uintptr_t)buf;
    vb->indirect_desc[1].len = bytecnt;
    vb->indirect_desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    vb->indirect_desc[1].next = 2;

    disable_interrupts();
//submit the request to the ring
    vb->avail.ring[0] = 0;
    __sync_synchronize();
    vb->avail.idx += 1;
    __sync_synchronize();

    virtio_notify_avail(vb->regs, 0);
//wait until completed
    while (vb->used.idx == vb->lui){
        condition_wait(&vb->used_updated);
        disable_interrupts();
    }
        

    __sync_synchronize();
    vb->lui += 1;

    enable_interrupts();
//release the lock and translate the device status to the result
    rwlock_release(&vb->lock);

    if (vb->status == VIRTIO_BLK_S_OK)
        return bytecnt;
    else if (vb->status == VIRTIO_BLK_S_UNSUPP)
        return -ENOTSUP;
    else
        return -EIO;
}


long vioblk_store (
    struct io * io,
    unsigned long long bytepos,
    const void * buf,
    long bytecnt)
{
    // YOUR CODE HERE
    struct vioblk_device * const vb = (struct vioblk_device *)io;
//check if the request is in the boundary
    if (bytecnt == 0)return 0;
    if (vb->size < bytepos || vb->size - bytepos < (unsigned long long)bytecnt) return -EINVAL;
//acquire the lock and biuld the block
    rwlock_acquire(&vb->lock, 1);

    vb->reqhdr.type = VIRTIO_BLK_T_OUT;
    vb->reqhdr.reserved = 0;
    vb->reqhdr.sector = bytepos / 512;
    vb->status = 0xFF;

    vb->indirect_desc[1].addr = (uint64_t)(uintptr_t)buf;
    vb->indirect_desc[1].len = bytecnt;
    vb->indirect_desc[1].flags = VIRTQ_DESC_F_NEXT;
    vb->indirect_desc[1].next = 2;

    disable_interrupts();
//submit it to an available ring
    vb->avail.ring[0] = 0;
    __sync_synchronize();
    vb->avail.idx += 1;
    __sync_synchronize();

    virtio_notify_avail(vb->regs, 0);
//wait until finished
    while (vb->used.idx == vb->lui){
        condition_wait(&vb->used_updated);
        disable_interrupts();
    }
        

    __sync_synchronize();
    vb->lui += 1;

    enable_interrupts();
//release the lock and then return
    rwlock_release(&vb->lock);

    if (vb->status == VIRTIO_BLK_S_OK) return bytecnt;
    else if (vb->status == VIRTIO_BLK_S_UNSUPP) return -ENOTSUP;
    else return -EIO;
}

int vioblk_ioctl(struct io * io, int op, void * arg) {
    // YOUR CODE HERE
    struct vioblk_device * const vb = (struct vioblk_device *)io;
    unsigned long long * const ullarg = arg;
//do the correct operation and reject unsupported ones.
    switch (op) {
    case IOC_GETEND:
        if (ullarg == NULL)
            return -EINVAL;
        *ullarg = vb->size;
        return 0; //success
    default:
        return -ENOTSUP;
    }
}

void vioblk_isr(int irqno, void * aux) {
    // YOUR CODE HERE
    struct vioblk_device * const vb = aux;
    uint32_t st;

    (void)irqno;
//read and catch the interrupt, wake spinning thread for finishing it.
    st = vb->regs->interrupt_status;
    vb->regs->interrupt_ack = st;
    __sync_synchronize();

    condition_broadcast(&vb->used_updated);
}