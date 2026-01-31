// fsimpl.h - File system implementation interface
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifndef _FSIMPL_H_
#define _FSIMPL_H_

#include "io.h"

struct filesystem {
    const char * implname;
    int (*openfile)(struct filesystem * fs, const char * flname, struct io ** ioptr);
    int (*createfile)(struct filesystem * fs, const char * flname);
    int (*deletefile)(struct filesystem * fs, const char * flname);
    void (*flush)(struct filesystem * fs);
};

#endif  // _FSIMPL_H_
