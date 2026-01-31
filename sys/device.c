// device.h - Device manager
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "device.h"

#include <limits.h>  // INT_MAX
#include <stddef.h>

#include "conf.h"
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "ioimpl.h"

// INTERNAL TYPE DEFINITIONS
//

// Device list entry

struct device_record {
    struct device_record * next;    // Next device record in linked list
    device_openfn_t openfn;         // Function for opening device
    void * ofaux;                   // Auxilliary argument for openfn
    char name[];                    // device name
};


// INTERNAL FUNCTION DECLARATIONS
//


struct device_record * find_device(const char * name);

// EXPORTED GLOBAL VARIABLES
//

char devmgr_initialized = 0;


// DEVMGR INTERNAL GLOBAL VARIABLES
//

static struct device_record * devlist;


// EXPORTED FUNCTION DEFINITIONS
//

void devmgr_init(void) {
    trace("%s()", __func__);
    devmgr_initialized = 1;
}

int register_device (
    const char * name,
    int instno,
    device_openfn_t openfn,
    void * ofaux)
{
    struct device_record * dev = NULL;
    size_t namelen;

    trace("%s(\"%s\", %d)", __func__, name, instno);

    assert(devmgr_initialized);
    assert(name != NULL);
    assert(openfn != NULL);

    namelen = strlen(name);

    if (999 < instno || 255 < namelen)
        return -EINVAL;

    dev = kmalloc(sizeof(*dev) + namelen+3+1);
    memset(dev, 0, sizeof(*dev));

    if (0 <= instno)
        snprintf(dev->name, namelen+3+1, "%s%d", name, instno);
    else
        memcpy(dev->name, name, namelen+1);

    // Check if the device is already registered
    
    if (find_device(dev->name) != NULL) {
        kfree(dev);
        return -EEXIST;
    }

    dev->openfn = openfn;
    dev->ofaux = ofaux;
    dev->next = devlist;
    devlist = dev;
    return 0;
}

extern int open_device(const char * name, struct io ** ioptr) {
    struct device_record * dev;

    trace("%s(\"%s\")", __func__, name);

    dev = find_device(name);

    return (dev != NULL) ? dev->openfn(ioptr, dev->ofaux) : -ENOENT;
}

// INTERNAL FUNCTION DEFINITIONS
//


struct device_record * find_device(const char * name) {
    struct device_record * dev;

    trace("%s(\"%s\")", __func__, name);

    for (dev = devlist; dev != NULL; dev = dev->next) {
        if (strcmp(name, dev->name) == 0)
            return dev;
    }

    return NULL;
}