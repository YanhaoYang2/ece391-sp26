/*! @file viorng.c 
    @brief VirtIO rng device
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "device.h"
#include "conf.h"
#include "intr.h"
#include "misc.h"
#include "ioimpl.h"

// INTERNAL CONSTANT DEFINITIONS
//



#ifndef VIORNG_NAME
#define VIORNG_NAME "viorng"
#endif

#ifndef VIORNG_IRQ_PRIO
#define VIORNG_IRQ_PRIO 1
#endif

// INTERNAL TYPE DEFINITIONS
//

struct viorng_device {
    // YOUR CODE HERE
    struct io io;

    volatile struct virtio_mmio_regs * regs;
    int irqno;

    uint16_t qlen;
    uint16_t last_used;

    struct condition done;
    struct rwlock lock;

    struct {
        uint64_t addr;
        uint32_t len;
        uint16_t flags;
        uint16_t next;
    } desc[1];

    struct {
        uint16_t flags;
        uint16_t idx;
        uint16_t ring[1];
        uint16_t used_event;
    } avail;

    volatile struct {
        uint16_t flags;
        uint16_t idx;
        struct {
            uint32_t id;
            uint32_t len;
        } ring[1];
    } used;
};

// INTERNAL FUNCTION DECLARATIONS
//

static int viorng_open(struct io ** ioptr, void * aux);

static void viorng_reclaim(struct io * io);

static long viorng_read(struct io * io, void * buf, long bufsz);

static void viorng_isr(int irqno, void * aux);

// INTERNAL GLOBAL VARIABLES
//

static const struct iointf viorng_intf = {
    .implname = "viorng",
    .reclaim = &viorng_reclaim,
    .read = &viorng_read
};

// EXPORTED FUNCTION DEFINITIONS
//

// The vioblk_attach function is declared and called from virtio_attach() in
// virtio.c when a VirtIO RNG device is found.

void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    static unsigned short instcnt = 0; // number of viorng devices
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct viorng_device * vrng;
    int result;
    
    assert (regs->device_id == VIRTIO_ID_RNG);

    // Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    // fence o,io
    __sync_synchronize();

    virtio_featset_init(needed_features);
    virtio_featset_init(wanted_features);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        debug("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // YOUR CODE HERE
    vrng = kcalloc(1, sizeof(*vrng));

    vrng->regs = regs;
    vrng->irqno = irqno;
    vrng->qlen = 1;
    vrng->last_used = 0;
    vrng->avail.idx = 0;
    vrng->avail.ring[0] = 0;
    vrng->used.idx = 0;

    condition_init(&vrng->done, "viorng.done");
    rwlock_init(&vrng->lock, "viorng.lock");

    regs->queue_sel = 0;
    __sync_synchronize();

    virtio_attach_virtq(
        regs, 0, vrng->qlen,
        (uint64_t)(uintptr_t)vrng->desc,
        (uint64_t)(uintptr_t)&vrng->used,
        (uint64_t)(uintptr_t)&vrng->avail
    );

    regs->status |= VIRTIO_STAT_FEATURES_OK;
    assert(regs->status & VIRTIO_STAT_FEATURES_OK);
    
    // YOUR CODE HERE
    ioinit(&vrng->io, &viorng_intf, 1, 0);  
    
    regs->status |= VIRTIO_STAT_DRIVER_OK; //set the driver to OK
    // fence o,oi
    __sync_synchronize();

    // YOUR CODE HERE
    register_device(VIORNG_NAME, instcnt++, &viorng_open, vrng);
}

int viorng_open(struct io ** ioptr, void * aux) {
    // YOUR CODE HERE
    struct viorng_device * const vrng = aux;


    if (!iorefcnt(&vrng->io)) {
        vrng->avail.idx = 0;
        vrng->used.idx = 0;
        vrng->last_used = 0;
        enable_intr_source(vrng->irqno, VIORNG_IRQ_PRIO, viorng_isr, vrng);
        virtio_enable_virtq(vrng->regs, 0);
    }

    *ioptr = ioaddref(&vrng->io);
    return 0;
}

void viorng_reclaim(struct io * io) {
    // YOUR CODE HERE
    struct viorng_device * const vrng = (struct viorng_device *)io;

    disable_intr_source(vrng->irqno);

    vrng->regs->queue_sel = 0;
    __sync_synchronize();
    vrng->regs->queue_ready = 0;

    vrng->regs->interrupt_ack = vrng->regs->interrupt_status;
    __sync_synchronize();
}

long viorng_read(struct io * io, void * buf, long bufsz) {
    // YOUR CODE HERE
    struct viorng_device * const vrng = (struct viorng_device *)io;
    char * p = buf;
    long left = bufsz;

    if (bufsz == 0)
        return 0;

    rwlock_acquire(&vrng->lock, 1);

    while (left > 0) {
        uint32_t c = (uint32_t)left;

        disable_interrupts();

        vrng->desc[0].addr = (uint64_t)(uintptr_t)p;
        vrng->desc[0].len = c;
        vrng->desc[0].flags = VIRTQ_DESC_F_WRITE;
        vrng->desc[0].next = 0;

        __sync_synchronize();

        vrng->avail.ring[0] = 0;
        __sync_synchronize();
        vrng->avail.idx++;
        __sync_synchronize();

        virtio_notify_avail(vrng->regs, 0);

        while (vrng->used.idx == vrng->last_used)
            condition_wait(&vrng->done);

        __sync_synchronize();

        {
            uint32_t g = vrng->used.ring[0].len;
            vrng->last_used++;
            enable_interrupts();
            p += g;
            left -= g;
        }
    }

    rwlock_release(&vrng->lock);
    return bufsz;
}

void viorng_isr(int irqno, void * aux) {
    // YOUR CODE HERE
    struct viorng_device * const vrng = aux;
    uint32_t st;

    (void)irqno;

    st = vrng->regs->interrupt_status;
    vrng->regs->interrupt_ack = st;
    __sync_synchronize();

    condition_broadcast(&vrng->done);
}