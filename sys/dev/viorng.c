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
struct virtq_used_elem {
        /* Index of start of used descriptor chain. */
        uint32_t id;
        /* Total length of the descriptor chain which was used (written to) */
        uint32_t len;
};
struct virtq_desc { 
        uint64_t addr;
        uint32_t len;
        uint16_t flags;
        int16_t next; 
};
struct virtq_avail {
        uint16_t flags;
        uint16_t idx;
        uint16_t ring[1];
};
 struct virtq_used {
        uint16_t flags;
        uint16_t idx;
        struct virtq_used_elem ring[1];
};


struct viorng_device {
    volatile struct virtio_mmio_regs * regs;
    // YOUR CODE HERE
    
    int irqno;
    struct io io;
    struct virtq_desc * desc_table;
    struct virtq_avail *avail;
    volatile struct virtq_used * used;
    struct condition viorng_cond;
    struct rwlock lock;
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
    
    regs->status |= VIRTIO_STAT_FEATURES_OK;
    assert(regs->status & VIRTIO_STAT_FEATURES_OK);
    
    // YOUR CODE HERE
    vrng = kcalloc(1, sizeof(*vrng));
    condition_init(&vrng->viorng_cond,"viorng_condition");
    rwlock_init(&vrng->lock,"rwlock");
    vrng->regs=regs;
    vrng->irqno=irqno;
    assert(regs->queue_ready==0x0);
    assert(regs->queue_num_max>0);
    vrng->desc_table=kcalloc(1,sizeof(*vrng->desc_table));
    vrng->avail=kcalloc(1,sizeof(*vrng->avail));
    vrng->used=kcalloc(1,sizeof(*vrng->used));
    vrng->used->idx=0;
    vrng->avail->idx=0;
    virtio_attach_virtq(regs,0,1,(uint64_t) vrng->desc_table,(uint64_t) vrng->used,(uint64_t) vrng->avail);
    virtio_enable_virtq(regs,0);
    regs->status |= VIRTIO_STAT_DRIVER_OK; //set the driver to OK
    // fence o,oi
    __sync_synchronize();
    register_device(VIORNG_NAME,instcnt++,&viorng_open, vrng);
    ioinit(&vrng->io,&viorng_intf,1,0);
    // YOUR CODE HERE
}

int viorng_open(struct io ** ioptr, void * aux) {
    // YOUR CODE HERE
    struct viorng_device * const vrng=aux;
    *ioptr= ioaddref(&vrng->io);
    enable_intr_source(vrng->irqno,1,viorng_isr,vrng);
    return 0;
}

void viorng_reclaim(struct io * io) {
    // YOUR CODE HERE
       struct viorng_device * const vrng =
        (void*)io - offsetof(struct viorng_device, io);
        disable_intr_source(vrng->irqno);
        virtio_reset_virtq(vrng->regs,0);
}

long viorng_read(struct io * io, void * buf, long bufsz) {
    // YOUR CODE HERE
           struct viorng_device * const vrng =
        (void*)io - offsetof(struct viorng_device, io);
        if(bufsz==0) return 0;
        // want exclusive clock to reach serialization
        rwlock_acquire(&vrng->lock,1);
        vrng->desc_table->addr=(uint64_t)buf;
        vrng->desc_table->len=bufsz;
        vrng->desc_table->flags =VIRTQ_DESC_F_WRITE;
        vrng->desc_table->next=0;
        vrng->avail->ring[0]=0;
        vrng->avail->idx+=1;
        __sync_synchronize();
        int s=disable_interrupts();
        virtio_notify_avail(vrng->regs,0);
        //condition wait for device to process available area;
        while(vrng->avail->idx!=vrng->used->idx){
            condition_wait(&vrng->viorng_cond);
        }
        restore_interrupts(s);
        rwlock_release(&vrng->lock);
        return vrng->used->ring[0].len;

}

void viorng_isr(int irqno, void * aux) {
    // YOUR CODE HERE
     struct viorng_device * const vrng=aux;
     vrng->regs->interrupt_ack=vrng->regs->interrupt_status;
    __sync_synchronize();
    //hey wake up used idx is equal to avail idx now
    condition_broadcast(&vrng->viorng_cond);
}