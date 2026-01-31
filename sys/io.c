// io.c - Generic I/O objects
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "io.h"
#include "ioimpl.h"
#include <stddef.h>
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "thread.h"

// IO EXPORTED FUNCTION DEFINITIONS
//

struct io * ioinit (
    struct io * io,
    const struct iointf * intf,
    unsigned int blksz,
    unsigned int refcnt)
{
    io->intf = intf;
    io->blksz = blksz;
    io->refcnt = refcnt;
    return io;
}

unsigned int ioblksz(const struct io * io) {
    return io->blksz;
}

unsigned int iorefcnt(const struct io * io) {
    return io->refcnt;
}

struct io * ioaddref(struct io * io) {
    io->refcnt += 1;

    if (io->refcnt == 0)
        panic("Too many io refs");
    
    return io;
}

void iodropref(struct io * io) {
    assert (io->refcnt != 0);
    io->refcnt -= 1;

    if (io->refcnt == 0 && io->intf->reclaim != NULL)
        io->intf->reclaim(io);
}

long ioread(struct io * io, void * buf, long bufsz) {
    assert (io != NULL);
    assert (io->refcnt != 0);
    assert (buf != NULL || bufsz == 0);

    if (io->intf->read == NULL)
        return -ENOTSUP;
    
    if (bufsz < 0)
        return -EINVAL;
    
    if (bufsz != 0 && bufsz < io->blksz)
        return -EINVAL;
    
    return io->intf->read(io, buf, ROUND_DOWN(bufsz, io->blksz));
}

long iofill(struct io * io, void * buf, long bufsz) {
    // YOUR CODE HERE
}

long iowrite(struct io * io, const void * buf, long buflen) {
    assert (io != NULL);
    assert (buf != NULL || buflen == 0);

    if (io->intf->write == NULL)
        return -ENOTSUP;
    
    if (buflen < 0)
        return -EINVAL;
    
    if (buflen != 0 && buflen < io->blksz)
        return -EINVAL;
    
    return io->intf->write(io, buf, ROUND_DOWN(buflen, io->blksz));
}

long iofetch(struct io * io, unsigned long long pos, void * buf, long buflen) {
    assert (io != NULL);
    assert (io->refcnt != 0);
    assert (buf != NULL || buflen == 0);

    if (io->intf->fetch == NULL)
        return -ENOTSUP;
    
    if (buflen < 0)
        return -EINVAL;
    
    if (buflen != 0 && buflen < io->blksz)
        return -EINVAL;
    
    if (pos % io->blksz != 0 || buflen % io->blksz != 0)
        return -EINVAL;
    
    return io->intf->fetch(io, pos, buf, buflen);
}

long iostore(struct io * io, unsigned long long pos, const void * buf, long buflen) {
    assert (io != NULL);
    assert (io->refcnt != 0);
    assert (buf != NULL || buflen == 0);

    if (io->intf->fetch == NULL)
        return -ENOTSUP;
    
    if (buflen < 0)
        return -EINVAL;
    
    if (buflen != 0 && buflen < io->blksz)
        return -EINVAL;
    
    if (pos % io->blksz != 0 || buflen % io->blksz != 0)
        return -EINVAL;
    
    return io->intf->store(io, pos, buf, buflen);
}

int ioctl(struct io * io, int op, void * arg) {
    assert (io != NULL);
    assert (io->refcnt != 0);

    if (op == IOC_GETBLKSZ)
        return io->blksz;
    
    if (io->intf->ioctl != NULL)
        return io->intf->ioctl(io, op, arg);
    else
        return -ENOTSUP;
}

int ioctl_u(struct io * io, int op, uintptr_t u_arg) {
    assert (io != NULL);
    assert (io->refcnt != 0);

    if (op == IOC_GETBLKSZ)
        return io->blksz;
    
    if (io->intf->ioctl_u != NULL)
        return io->intf->ioctl_u(io, op, u_arg);
    else
        return -ENOTSUP;
}

// SEEKIO EXPORTED FUNCTION DEFINITIONS
//

struct io * seekio_init (
    struct seekio * sio,
    const struct iointf * intf,
    unsigned long long endpos,
    unsigned int blksz,
    unsigned int refcnt)
{
    sio->pos = 0;
    sio->end = endpos;
    return ioinit(&sio->base, intf, blksz, refcnt);
}

long seekio_read(struct io * io, void * buf, long bufsz) {
    struct seekio * const sio = (struct seekio*)io;
    long reqlen, retlen;

    assert (sio->pos % io->blksz == 0);
    assert (sio->end % io->blksz == 0);

    if (io->intf->fetch == NULL)
        return -ENOTSUP;
    
    if (sio->pos == sio->end)
        return 0;
    
    if (sio->end - sio->pos < bufsz)
        reqlen = sio->end - sio->pos;
    else
        reqlen = ROUND_DOWN(bufsz, sio->base.blksz);
    
    retlen = io->intf->fetch(&sio->base, sio->pos, buf, reqlen);

    if (retlen <= 0)
        return retlen;
    
    assert (retlen == reqlen);
    sio->pos += retlen;
    return retlen;
}

long seekio_write(struct io * io, const void * buf, long len) {
    struct seekio * const sio = (struct seekio*)io;
    long reqlen, retlen;
    long new_end;

    if (io->intf->store == NULL)
        return -ENOTSUP;
    
    if (sio->end - sio->pos < len) {
        new_end = sio->pos + len;
        ioctl(io, IOC_SETEND, &new_end);
        reqlen = sio->end - sio->pos;
    }
    else
        reqlen = ROUND_DOWN(len, sio->base.blksz);
    
    if (reqlen == 0)
        return 0;
    
    retlen = io->intf->store(&sio->base, sio->pos, buf, reqlen);

    if (retlen <= 0)
        return retlen;
    
    assert (retlen == reqlen);
    sio->pos += retlen;
    return retlen;
}

int seekio_ioctl(struct io * io, int op, void * arg) {
    struct seekio * const sio = (struct seekio*)io;
    unsigned long long * const ullarg = arg;

    switch (op) {
    case IOC_GETEND:
        *ullarg = sio->end;
        return 0;
    
    case IOC_GETPOS:
        *ullarg = sio->pos;
        return 0;

    case IOC_SETPOS:
        if (*ullarg % io->blksz != 0)
            return -EINVAL;
        if (sio->end < *ullarg)
            return -EINVAL;
        sio->pos = *ullarg;
        return 0;
    
    default:
        return -ENOTSUP;
    }
}

void seekio_resized(struct seekio * sio, unsigned long long endpos) {
    sio->end = endpos;
}


// NULLIO INTERNAL FUNCTION DECLARATIONS
//

static long nullio_read(struct io * io, void * buf, long bufsz);
static long nullio_write(struct io * io, const void * buf, long len);
static long nullio_fetch(struct io * io, unsigned long long pos, void * buf, long len);
static long nullio_store(struct io * io, unsigned long long pos, const void * buf, long len);


// NULLIO INTERNAL GLOBAL VARIABLES
//

static const struct iointf nullio_intf = {
    .implname = "null",
    .read = &nullio_read,
    .write = &nullio_write,
    .fetch = &nullio_fetch,
    .store = &nullio_store
};

static struct io nullio = {
    .intf = &nullio_intf,
    .blksz = 1,
    .refcnt = 0
};

// NULLIO INTERNAL FUNCTION DEFINITIONS
//

struct io * create_nullio(void) {
    return ioaddref(&nullio);
}

long nullio_read(struct io * io, void * buf, long bufsz) {
    (void)io;
    (void)buf;
    (void)bufsz;
    return 0;
}

long nullio_write(struct io * io, const void * buf, long len) {
    (void)io;
    (void)buf;
    (void)len;
    return 0;
}

long nullio_fetch(struct io * io, unsigned long long pos, void * buf, long len) {
    assert (io == &nullio);
    (void)buf;

    if (pos != 0 || len != 0)
        return -EINVAL;
    
    return 0;
}

long nullio_store(struct io * io, unsigned long long pos, const void * buf, long len) {
    assert (io == &nullio);
    (void)buf;

    if (pos != 0 || len != 0)
        return -EINVAL;
    
    return 0;
}

// MEMIO INTERNAL TYPE DEFINITIONS
//

struct memio {
    struct seekio base;
    void * buf;
    void (*reclfn)(void*,size_t);
};

// MEMIO INTERNAL FUNCTION DECLARATIONS
//

static void memio_reclaim(struct io * io);
static long memio_fetch(struct io * io, unsigned long long pos, void * buf, long len);
static long memio_store(struct io * io, unsigned long long pos, const void * buf, long len);

// MEMIO INTERNAL CONSTANT DEFINITIONS
//

static const struct iointf memio_intf = {
    .implname = "memio",
    .reclaim = &memio_reclaim,
    .read = &seekio_read,
    .write = &seekio_write,
    .fetch = &memio_fetch,
    .store = &memio_store,
    .ioctl = &seekio_ioctl
};

// MEMIO EXPORTED FUNCTION DEFINITIONS
//

struct io * create_memio (
    void * buf, size_t size,
    void(*reclfn)(void*,size_t))
{
    // YOUR CODE HERE
}

// MEMIO INTERNAL FUNCTION DEFINITIONS
//

void memio_reclaim(struct io * io) {
    struct memio * const mio = (struct memio*)io;

    if (mio->reclfn != NULL)
        mio->reclfn(mio->buf, mio->base.end);
    
    kfree(mio);
}

long memio_fetch(struct io * io, unsigned long long pos, void * buf, long len) {
    // YOUR CODE HERE
}

long memio_store(struct io * io, unsigned long long pos, const void * buf, long len) {
    // YOUR CODE HERE
}
