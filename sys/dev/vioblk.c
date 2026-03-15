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
//self defined virtq length
#define VIRTQ_LEN 4

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
        uint16_t ring[VIRTQ_LEN];
};
 struct virtq_used {
        uint16_t flags;
        uint16_t idx;
        struct virtq_used_elem ring[VIRTQ_LEN];
};

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
      volatile struct virtio_mmio_regs * regs;
    // YOUR CODE HERE
    int irqno;
    int blk_size;
    int capacity;
    struct io io;
    struct virtq_desc desc_table[VIRTQ_LEN] ;
    struct virtq_avail *avail;
    volatile struct virtq_used * used;
    struct condition vioblk_cond;
    struct rwlock lock;              
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
    regs->status|=VIRTIO_STAT_FEATURES_OK;
    assert(regs->status & VIRTIO_STAT_FEATURES_OK);

    vb = kcalloc(1, sizeof(*vb));
    vb->blk_size=blksz;
    // get the capacity max sectors and save it;
    vb->capacity=regs->config.blk.capacity;
    // condition and rwlock initiated
    condition_init(&vb->vioblk_cond,"vioblk_condition");
    rwlock_init(&vb->lock,"vioblk_rwlock");
    vb->regs=regs;
    vb->irqno=irqno;
    // Checkif the queueis not already in use
    assert(regs->queue_ready==0x0);
    // Read maximum queue size (number of elements) from QueueNumMax. 
    assert(regs->queue_num_max>0);
    //allocate and zero the queue
    vb->avail = kcalloc(1, sizeof(*vb->avail));
    vb->used = kcalloc(1, sizeof(*vb->used));
    // zero the descriptor table
    for(int i=0;i<VIRTQ_LEN;i++){
        vb->desc_table[i].addr=0;
        vb->desc_table[i].flags=0;
        vb->desc_table[i].len=0;
        vb->desc_table[i].next=0;
    }
    //initailize the index 
    vb->used->idx=0;
    vb->avail->idx=0;
    virtio_attach_virtq(regs,0,4,(uint64_t)vb->desc_table,(uint64_t) vb->used,(uint64_t) vb->avail);
    virtio_enable_virtq(regs,0);
        __sync_synchronize();
    register_device(VIOBLK_NAME,instcnt++,&vioblk_open,vb);
    ioinit(&vb->io,&vioblk_intf,1,0);
}

int vioblk_open(struct io ** ioptr, void * aux) {
    // YOUR CODE HERE
    struct vioblk_device * const vb=aux;
    *ioptr= ioaddref(&vb->io);
    // interrupt enabled at open
    enable_intr_source(vb->irqno,1,vioblk_isr,vb);
    return 0;
}

void vioblk_reclaim(struct io * io) {
    // YOUR CODE HERE
           struct vioblk_device * const vb =
        (void*)io - offsetof(struct vioblk_device, io);
        // interrupt disabled for this device
        disable_intr_source(vb->irqno);
        virtio_reset_virtq(vb->regs,0);
}

long vioblk_fetch (
    struct io * io,
    unsigned long long bytepos,
    void * buf,
    long bytecnt)
{
    // YOUR CODE HERE
    struct vioblk_device * const vb =
        (void*)io - offsetof(struct vioblk_device, io);
    // if past the end of sector, return error code invalid argument
    if(bytecnt+bytepos> vb->capacity*vb->blk_size) 
    {return -EINVAL;}
    rwlock_acquire(&vb->lock,1);
     struct vioblk_request_header vb_reqeust; 
    uint8_t status;     
    vb_reqeust.type=VIRTIO_BLK_T_IN;
    vb_reqeust.sector=bytepos/vb->blk_size;
    
    // three tables first request, next data buffer, final the status
    vb->desc_table[0].addr=(uint64_t)&vb_reqeust;
    vb->desc_table[0].len=sizeof(vb_reqeust);
    // indicating it is chained and read only
    vb->desc_table[0].flags=VIRTQ_DESC_F_NEXT;
    vb->desc_table[0].next=1;
    vb->desc_table[1].addr=(uint64_t)buf;
    vb->desc_table[1].len=bytecnt;
    // indicating it is chained and writable
    vb->desc_table[1].flags=VIRTQ_DESC_F_NEXT|VIRTQ_DESC_F_WRITE;
    vb->desc_table[1].next=2;
    vb->desc_table[2].addr=(uint64_t)&status;
    //uint_8
    vb->desc_table[2].len=1;
    //end of the chain and writable
    vb->desc_table[2].flags=VIRTQ_DESC_F_WRITE;
    //end of the chain 
    vb->desc_table[2].next=0;
    vb->avail->ring[vb->avail->idx%VIRTQ_LEN]=0;
    vb->avail->idx++;
     __sync_synchronize();
     int s=disable_interrupts();
    virtio_notify_avail(vb->regs,0);
        //condition wait for device to process available area;
        while(vb->avail->idx!=vb->used->idx){
            condition_wait(&vb->vioblk_cond);
        }
    restore_interrupts(s);
    rwlock_release(&vb->lock);
    return bytecnt;
}


long vioblk_store (
    struct io * io,
    unsigned long long bytepos,
    const void * buf,
    long bytecnt)
{
    // YOUR CODE HERE
        struct vioblk_device * const vb =
        (void*)io - offsetof(struct vioblk_device, io);
    // if past the end of sector, return error code invalid argument
    if(bytecnt+bytepos> vb->capacity*vb->blk_size) 
    {return -EINVAL;}
    rwlock_acquire(&vb->lock,1);
    struct vioblk_request_header vb_reqeust; 
    uint8_t status;     
    vb_reqeust.type=VIRTIO_BLK_T_OUT;
   vb_reqeust.sector=bytepos/vb->blk_size;
      // three tables first request, next data buffer, final the status
    vb->desc_table[0].addr=(uint64_t)&vb_reqeust;
    vb->desc_table[0].len=sizeof( vb_reqeust);
    // indicating it is chained and read only
    vb->desc_table[0].flags=VIRTQ_DESC_F_NEXT;
    vb->desc_table[0].next=1;
    vb->desc_table[1].addr=(uint64_t)buf;
    vb->desc_table[1].len=bytecnt;
    // indicating it is chained and read only
    vb->desc_table[1].flags=VIRTQ_DESC_F_NEXT;
    vb->desc_table[1].next=2;
    vb->desc_table[2].addr=(uint64_t)&status;
    //uint_8
    vb->desc_table[2].len=1;
    //end of the chain and writable
    vb->desc_table[2].flags=VIRTQ_DESC_F_WRITE;
    //end of the chain 
    vb->desc_table[2].next=0;
    vb->avail->ring[vb->avail->idx%VIRTQ_LEN]=0;
    vb->avail->idx++;
     __sync_synchronize();
     int s=disable_interrupts();
    virtio_notify_avail(vb->regs,0);
        //condition wait for device to process available area;
        while(vb->avail->idx!=vb->used->idx){
            condition_wait(&vb->vioblk_cond);
        }
    restore_interrupts(s);
    rwlock_release(&vb->lock);
    return bytecnt;
}

int vioblk_ioctl(struct io * io, int op, void * arg) {
    // YOUR CODE HERE
     struct vioblk_device * const vb =
        (void*)io - offsetof(struct vioblk_device, io);
        unsigned long long * const ullarg=arg;
        // handle get end operation which is size*capacity
    if(op==IOC_GETEND){
        *ullarg =vb->blk_size*vb->capacity;
        return 0;
    }else {// other operations not supported
        return -ENOTSUP;
    }
}

void vioblk_isr(int irqno, void * aux) {
    // YOUR CODE HERE
      struct vioblk_device * const vb=aux;
     vb->regs->interrupt_ack=vb->regs->interrupt_status;
    __sync_synchronize();
    //hey wake up used idx is equal to avail idx now
    condition_broadcast(&vb->vioblk_cond);
}