// process.c - Processes and process manager
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//


#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif

#include "process.h"

#include "conf.h"
#include "elf.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "memory.h"
#include "misc.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"
#include "trap.h"
#include "io.h"
#include "intr.h"
// INTERNAL FUNCTION DECLARATIONS
//

static int build_stack(void * stack, int argc, char ** argv);

static void fork_func(struct condition * forked, struct trap_frame * tfr);

// INTERNAL GLOBAL VARIABLES
//

static struct process main_proc;

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

void procmgr_init(void) {
    assert(memory_initialized && heap_initialized);
    assert(!procmgr_initialized);

    main_proc.tid = running_thread();
    main_proc.mtag = active_mspace();
    thread_attach_process(main_proc.tid, &main_proc);
    procmgr_initialized = 1;
}

int process_exec(struct io * exeio, int argc, char ** argv) {
    // YOUR CODE HERE
    void * pg_stack;
    int stksz;
    struct process * proc=current_process();
    // /Copies arguments from user space to a new page
    pg_stack=alloc_phys_page();
    assert(pg_stack!=NULL);
    stksz=build_stack(pg_stack,argc,argv);
    // if error code, free assigned page, return error code directly
    if(stksz<0){
        free_phys_page(pg_stack);
        return stksz;
    }
    //Calls reset_active_mspace to unmap all pages mapped into user address space and frees the backing physical pages
    reset_active_mspace();
    //Calls elf_load to load and map program image
    //prepares an entry function pointer
    void (*eptr)(void);
    int success= elf_load(exeio,&eptr);
    iodropref(exeio);
    if(success!=0){
        free_phys_page(pg_stack);
        return success;
    }
    map_page(UMEM_END_VMA-PAGE_SIZE,pg_stack,PTE_R|PTE_W|PTE_U);

    
    //zero initializes the trap frame
    struct  trap_frame t1={0};
    t1.sepc=eptr;
    t1.a0=argc;
    //(UMEM_END_VMA - stksz) maps to stack + PAGE_SIZE - stksz in physical memory it is the stack pointer(growing downwards) and also the origin of the argv arrat (growing upwards)
    t1.a1=(uintptr_t *)(UMEM_END_VMA - stksz);
    t1.sp=(void *)(UMEM_END_VMA - stksz);

    t1.sstatus=csrr_sstatus();
    //back to user mode
    t1.sstatus &= ~RISCV_SSTATUS_SPP;
    //interrupt enabled upon swtiching to user mode
    t1.sstatus |= RISCV_SSTATUS_SPIE;
    void * scratch=(char *)running_thread_stack_anchor()-sizeof(struct trap_frame);
    trap_frame_jump(&t1,scratch);

}

int process_fork(const struct trap_frame * tfr) {
    // YOUR CODE HERE
    struct process * parent=current_process();
    //try to allocate and initialize space for child process
    struct process * child=kmalloc(sizeof(struct process));
    if(child==NULL){
        return -ENOMEM;
    }
    memset(child,0,sizeof(struct process));
    //clone address space;
    child->mtag=clone_active_mspace();
    //copy parent's io pointers with ioaddref
    for(int i=0;i<PROC_IOMAX;i++){
        if(parent->iotab[i]!=NULL){
            child->iotab[i]=ioaddref(parent->iotab[i]);
        }
    }
    //Spawn a thread and attach it to the child process
    struct condition done;
    condition_init(&done, "fork_done");
    // avoid timer interrupt let child runs before conditin wait
    int pre=disable_interrupts();
    int tid=spawn_thread("fork_entry_thread", (void (*)(void))fork_func,&done,(struct trap_frame *)tfr);
    // if spawn failed, we need to clear iotab, discard memory space and free memory used
    if(tid<0){
        for(int i=0;i<PROC_IOMAX;i++){
            if(child->iotab[i]!=NULL){
                iodropref(child->iotab[i]);
                child->iotab[i] = NULL;
            }
        }
         //switch to child mtag, discard it and switch back;
        mtag_t cur_tag=switch_mspace(child->mtag);
        discard_active_mspace();
        switch_mspace(cur_tag);
        
        kfree(child);
        restore_interrupts(pre);    
        return tid;
    }
    child->tid=tid;
    thread_attach_process(tid,child);
    condition_wait(&done);
    restore_interrupts(pre);

    return tid;
}

void process_exit(void) {
    // YOUR CODE HERE
    // if it is main process,flush and halt
    struct process * proc = current_process();
    if(proc==&main_proc){
        flush_all_filesys();
        shutdown();
    }else{
        //drop reference to all valid members of calling process's iotab
        for(int i=0;i<PROC_IOMAX;i++){
            if(proc->iotab[i]!=NULL){
                iodropref(proc->iotab[i]);
                proc->iotab[i] = NULL;
            }
        }

        //discard the active memory space
        discard_active_mspace();
        //detach the  process from running thread, free memory allocated for the process, and exit running thread;
        thread_attach_process(running_thread(),NULL);
        kfree(proc);
        exit_running_thread();

    }

}

// INTERNAL FUNCTION DEFINITIONS
//

int build_stack(void * stack, int argc, char ** argv) {
    size_t stksz, argsz;
    uintptr_t * newargv;
    char * p;
    int i;

    // We need to be able to fit argv[] on the initial stack page, so _argc_
    // cannot be too large. Note that argv[] contains argc+1 elements (last one
    // is a NULL pointer).

    if (PAGE_SIZE / sizeof(char*) - 1 < argc)
        return -ENOMEM;
    
    stksz = (argc+1) * sizeof(char*);

    // Add the sizes of the null-terminated strings that argv[] points to.

    for (i = 0; i < argc; i++) {
        argsz = strlen(argv[i])+1;
        if (PAGE_SIZE - stksz < argsz)
            return -ENOMEM;
        stksz += argsz;
    }

    // Round up stksz to a multiple of 16 (RISC-V ABI requirement).

    stksz = ROUND_UP(stksz, 16);
    assert (stksz <= PAGE_SIZE);

    // Set _newargv_ to point to the location of the argument vector on the new
    // stack and set _p_ to point to the stack space after it to which we will
    // copy the strings. Note that the string pointers we write to the new
    // argument vector must point to where the user process will see the stack.
    // The user stack will be at the highest page in user memory, the address of
    // which is `(UMEM_END_VMA - PAGE_SIZE)`. The offset of the _p_ within the
    // stack is given by `p - newargv'.

    newargv = stack + PAGE_SIZE - stksz;
    p = (char*)(newargv+argc+1);

    for (i = 0; i < argc; i++) {
        newargv[i] = (UMEM_END_VMA - PAGE_SIZE) + ((void*)p - (void*)stack);
        argsz = strlen(argv[i])+1;
        memcpy(p, argv[i], argsz);
        p += argsz;
    }

    newargv[argc] = 0;
    return stksz;
}

void fork_func(struct condition * done, struct trap_frame * tfr) {
    // YOUR CODE HERE
    // make sure return value of child fork is 0;
    tfr->a0=0;
    //finished using the tfr
    condition_broadcast(done);
    void * scratch=(char *)running_thread_stack_anchor()-sizeof(struct trap_frame);
    trap_frame_jump(tfr,scratch);
    return;
}