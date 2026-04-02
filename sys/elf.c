// elf.c - ELF executable loader
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef ELF_TRACE
#define TRACE
#endif

#ifdef ELF_DEBUG
#define DEBUG
#endif

#include "elf.h"
#include "conf.h"
#include "io.h"
#include "string.h"
#include "memory.h"
#include "error.h"
#include "misc.h"

#include <stdint.h>

// Offsets into e_ident

#define EI_CLASS        4   
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7
#define EI_ABIVERSION   8   
#define EI_PAD          9  


// ELF header e_ident[EI_CLASS] values

#define ELFCLASSNONE 0
#define ELFCLASS32 1
#define ELFCLASS64 2

// ELF header e_ident[EI_DATA] values

#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

// ELF header e_ident[EI_VERSION] values

#define EV_NONE     0
#define EV_CURRENT  1

// ELF header e_type values

enum elf_et {
    ET_NONE = 0,
    ET_REL,
    ET_EXEC,
    ET_DYN,
    ET_CORE
};

// The /elf64_ehdr/ structure.
// ELF header struct
struct elf64_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff; 
    uint32_t e_flags; 
    uint16_t e_ehsize; 
    uint16_t e_phentsize; 
    uint16_t e_phnum; 
    uint16_t e_shentsize; 
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};


// The /elf_pt/ enumeration.
// Program header p_type values
enum elf_pt {
	PT_NULL = 0, 
	PT_LOAD,
	PT_DYNAMIC,
	PT_INTERP,
	PT_NOTE,
	PT_SHLIB,
	PT_PHDR,
	PT_TLS
};

// Program header p_flags bits

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// The /elf64_phdr/ structure.
// Program header struct
struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

// ELF header e_machine values (short list)

#define  EM_RISCV   243

int elf_load(struct io * io, void (**eptr)(void)) {
    // YOUR CODE HERE

//parameter test: check for invalid parameter
    if(io == NULL || eptr == NULL){
        return -EINVAL;
    }
//declare the ehdr and phdr
    struct elf64_ehdr e_header;
    struct elf64_phdr p_header;
//read the elf header    
    iofetch(io, 0, &e_header, sizeof(e_header));
//check the constant signiture
    if(e_header.e_ident[0] != 0x7f) return -EBADF;
    if(e_header.e_ident[1] != 'E') return -EBADF;
    if(e_header.e_ident[2] != 'L') return -EBADF;
    if(e_header.e_ident[3] != 'F') return -EBADF;
//check if the elf format is consistent
    if(e_header.e_ident[EI_CLASS] != ELFCLASS64) return -EBADF;
    if(e_header.e_ident[EI_DATA] != ELFDATA2LSB) return -EBADF;
    if(e_header.e_ident[EI_VERSION] != EV_CURRENT) return -EBADF;
    if(e_header.e_version != EV_CURRENT) return -EBADF;
    if(e_header.e_type != ET_EXEC) return -EBADF;
    if(e_header.e_machine != EM_RISCV) return -EBADF;
    if(e_header.e_ehsize != sizeof(struct elf64_ehdr)) return -EBADF;//ensure that the ELF header fields describe a valid layout
    if(e_header.e_phentsize != sizeof(struct elf64_phdr)) return -EBADF;
    if(e_header.e_entry == 0) return -EBADF;//validate the entry point
//iterate through all program headers
    for(uint16_t i = 0; i < e_header.e_phnum; i++){
        //read the program header from the table
        iofetch(io, e_header.e_phoff + (uint64_t)i * e_header.e_phentsize, &p_header, sizeof(p_header));
        if(p_header.p_type == PT_LOAD){
            if(p_header.p_memsz < p_header.p_filesz) return -EBADF;

            iofetch(io, p_header.p_offset, (void*)(uintptr_t) p_header.p_vaddr, p_header.p_filesz);
            //zero fill the remaining memory
            if(p_header.p_memsz > p_header.p_filesz) 
                memset((void *)(uintptr_t)p_header.p_vaddr + p_header.p_filesz, 0, p_header.p_memsz - p_header.p_filesz);
            
        }else continue;
    }

    *eptr = (void (*)(void))(uintptr_t) e_header.e_entry; //return the entry point

    return 0;
}