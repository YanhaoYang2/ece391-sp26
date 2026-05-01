// syscall.c - System call handling
//
// Copyright (c) 2024-2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include <stdint.h>
#ifdef SYSCALL_TRACE
#define TRACE
#endif

#ifdef SYSCALL_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "console.h"
#include "device.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "intr.h"
#include "memory.h"
#include "misc.h"
#include "process.h"
#include "scnum.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "io.h"

#ifndef PATH_MAX
// Maximum path name length. Must be less than HEAP_ALLOC_MAX.
#define PATH_MAX 255
#endif

// for ab1043 age calculations
#define NSEC_PER_SEC (1000000000UL)
#define NSEC_PER_MIN (60UL * NSEC_PER_SEC)
#define NSEC_PER_HR (60UL * NSEC_PER_MIN)
#define NSEC_PER_DAY (24UL * NSEC_PER_HR)
#define TICK_TO_DAYS(t) ((t) / NSEC_PER_DAY)

#define DAYS_TO_YR_APPX(d) ((d) * 400 / 146097)
#define LEAP_YEAR(y) (((y)%4 == 0 && (y)%100 != 0) || ((y)%400 == 0))

// EXPORTED FUNCTION DECLARATIONS
//

extern void handle_syscall(struct trap_frame *tfr);  // called from excp.c


// INTERNAL FUNCTION DECLARATIONS
//

static long syscall(const struct trap_frame *tfr);

static int sysexit(void);
static int sysexec(int fd, int argc, char ** argv);
static int sysfork(const struct trap_frame * tfr);
static int syswait(int tid);
static int sysprint(const char * msg);
static int sysusleep(unsigned long us);

static int sysdelete(const char * path);
static int syscreate(const char * path);

static int sysopen(int fd, const char * path);
static int sysclose(int fd);
static long sysread(int fd, void * buf, size_t bufsz);
static long syswrite(int fd, const void * buf, size_t len);
static int sysioctl(int fd, int cmd, uintptr_t arg_uma);
static int syspipe(int * wfd, int * rfd);
static int sysiodup(int oldfd, int newfd);
static int sysab1043(unsigned long long dob);

static int allocfd(struct process * proc, int reqfd, int notfd);

// EXPORTED FUNCTION DEFINITIONS
//

void handle_syscall(struct trap_frame * tfr) {
    // YOUR CODE HERE
    tfr->sepc +=4; //continue from next instruction;
    //a0 contains the return value
    tfr->a0=syscall(tfr);
    return;
}

// INTERNAL FUNCTION DEFINITIONS
//

long syscall(const struct trap_frame * tfr) {
    // YOUR CODE HERE
    //inner dispatch logic, dispatch on value in a7 register
    switch (tfr->a7){
        case SYSCALL_PRINT: 
            return sysprint((const char*)tfr->a0);

        case SYSCALL_EXIT:
            return sysexit();
        
        case SYSCALL_EXEC:
            return sysexec(tfr->a0,tfr->a1,(char **)tfr->a2);
        
        case SYSCALL_FORK:
            return sysfork(tfr);

        case SYSCALL_WAIT:
            return syswait(tfr->a0);
        
        case SYSCALL_USLEEP:
            return sysusleep(tfr->a0);
        
        case SYSCALL_CREATE:
            return syscreate((const char *) tfr->a0);
        
        case SYSCALL_DELETE:
            return sysdelete((const char *) tfr->a0);
        
        case SYSCALL_OPEN:
            return sysopen(tfr->a0,(const char *)tfr->a1);

        case SYSCALL_CLOSE:
            return sysclose(tfr->a0);
        
        case SYSCALL_READ:
            return sysread(tfr->a0,(void *)tfr->a1,tfr->a2);
        
        case SYSCALL_WRITE:
            return syswrite(tfr->a0,(void *)tfr->a1,tfr->a2);
        
        case SYSCALL_IOCTL:
            return sysioctl(tfr->a0, tfr->a1, tfr->a2);
        
        case SYSCALL_PIPE:
            return syspipe((int *)tfr->a0,(int *)tfr->a1);
        
        case SYSCALL_IODUP:
            return sysiodup(tfr->a0,tfr->a1);
        
        case SYSCALL_AB1043:
            return sysab1043(tfr->a0);
        
        default:
            return -ENOTSUP;
    }
    return 0;
}

int sysexit(void) {
    // YOUR CODE HERE
    //it will not return 
    process_exit();
    // should never reach here
    return 0;
}

int sysexec(int fd, int argc, char ** argv) {
    // YOUR CODE HERE
    struct process * proc =current_process();
    //check for bad file descriptor
    if(fd<0 || fd >=PROC_IOMAX || proc->iotab[fd]==NULL){
        return -EBADF;
    }
    struct io * exeio=proc->iotab[fd];
    //clear the iotab[fd] so it is properly closed when proc_exec drops a reference to it
    proc->iotab[fd]=NULL;
    return process_exec(exeio,argc,argv);

}

int sysfork(const struct trap_frame * tfr) {
    // YOUR CODE HERE
    return process_fork(tfr);
}

int syswait(int tid) {
    // YOUR CODE HERE
    //validate the thread id
    if(tid<0||tid>=NTHR){
        return -EINVAL;
    }
    return join_thread(tid);

}

int sysprint(const char *msg) {
    // YOUR CODE HERE
    int result;
    trace("%s(msg=%p)",__func__,msg);
    result = validate_vstr(msg,PTE_U);
    if(result != 0){
        return result;
    }
    kprintf("Thread <%s:%d> says: %s\n", thread_name(running_thread()),running_thread,msg);
    return 0;
}

int sysusleep(unsigned long us) {
    // YOUR CODE HERE
    //put current thread into us microseconds sleep  
    sleep_us(us);
    return 0;
}

int syscreate(const char *path) {
    // YOUR CODE HERE
    int result;
    //kernel path to avoid parse_path's destruction on read only path
    char kpath[PATH_MAX+1];
    char * mpname_ptr;
    char * filename_ptr;
    //validate path is a valid user-accessible string
    result= validate_vstr(path,PTE_U);
    if(result !=0){
        return result;
    }
    //make sure kpath is null terminated valid string
    memcpy(kpath,path,strlen(path)+1);
    parse_path(kpath,&mpname_ptr,&filename_ptr);
    return create_file(mpname_ptr,filename_ptr);
}

int sysdelete(const char * path) {
    // YOUR CODE HERE
    int result;
    //kernel path to avoid parse_path's destruction on read only path
    char kpath[PATH_MAX+1];
    char * mpname_ptr;
    char * filename_ptr;
    //validate path is a valid user-accessible string
    result= validate_vstr(path,PTE_U);
    if(result !=0){
        return result;
    }
    //make sure kpath is null terminated valid string
    memcpy(kpath,path,strlen(path)+1);
    parse_path(kpath,&mpname_ptr,&filename_ptr);
    return delete_file(mpname_ptr,filename_ptr);

}

int sysopen(int fd, const char * path) {
    // YOUR CODE HERE
    int result;
    //kernel path to avoid parse_path's destruction on read only path
    char kpath[PATH_MAX+1];
    char * mpname_ptr;
    char * filename_ptr;
    struct process * proc =current_process();
    int new_slot=-1;
    struct io * io = NULL;
    //validate path is a valid user-accessible string
    result= validate_vstr(path,PTE_U);
    if(result !=0){
        return result;
    }
    //allocate slot
    //check fot validaity of the fd
    if(fd>=0){
        if(fd>=PROC_IOMAX){
            return -EBADF;
        }
        //desired slot not available 
        if(proc->iotab[fd]!=NULL){
            return -EMFILE;
        }
        new_slot=fd;
    }else{
    int empty_slot=-1;
    for(int i=0;i<PROC_IOMAX;i++){
        if(proc->iotab[i]==NULL){
            empty_slot=i;
            break;
        }
    }
    // all slot busy 
    if(empty_slot==-1){
        return -EMFILE;
    }
    new_slot=empty_slot;
    }
    //make sure kpath is null terminated valid string
    memcpy(kpath,path,strlen(path)+1);
    parse_path(kpath,&mpname_ptr,&filename_ptr);
    // temporarily open file in a local io pointer
    result = open_file(mpname_ptr, filename_ptr, &io);
    if(result !=0){
        return result;
    }
    proc->iotab[new_slot] = io; // mount io object into the allocated slot after open succeeds
    return new_slot;
}

int sysclose(int fd) {
    // YOUR CODE HERE
    struct process * proc =current_process();
    if(fd<0|| fd>=PROC_IOMAX || proc->iotab[fd]==NULL){
        return -EBADF;
    }
    iodropref(proc->iotab[fd]);
    proc->iotab[fd]=NULL;
    return 0;
}

long sysread(int fd, void * buf, size_t bufsz) {
    // YOUR CODE HERE
    int result;
    struct process * proc =current_process();
    if(fd<0|| fd>=PROC_IOMAX || proc->iotab[fd]==NULL){
        return -EBADF;
    }
    //make sure buffer is user writable
    result=enforce_vptr(buf, bufsz,PTE_U|PTE_W);
    if(result !=0){
        return result;
    }
    return ioread(proc->iotab[fd],buf,bufsz);
}

long syswrite(int fd, const void *buf, size_t len) {
    // YOUR CODE HERE
    int result;
    struct process * proc =current_process();
    if(fd<0|| fd>=PROC_IOMAX || proc->iotab[fd]==NULL){
        return -EBADF;
    }
    //make sure buffer is user readable
    result=enforce_vptr(buf,len, PTE_U|PTE_R);
    if(result !=0){
        return result;
    }
    return iowrite(proc->iotab[fd],buf,len);
}

int sysioctl(int fd, int op, uintptr_t arg_uma) {
    // YOUR CODE HERE
    int result;
    struct process * proc =current_process();
    if(fd<0|| fd>=PROC_IOMAX || proc->iotab[fd]==NULL){
        return -EBADF;
    }
    switch (op)
    {
    case IOC_GETBLKSZ:
    //no arg return value is block size
        return ioctl(proc->iotab[fd],op,NULL);
    case IOC_RESET:
    //no arg, return value is 0 or not support
        return ioctl(proc->iotab[fd],op,NULL);
    case IOC_GETEND:
    // arg is unsigned long long *
        result = enforce_vptr((void *)arg_uma, sizeof(unsigned long long),PTE_U|PTE_W );
        if(result!=0){
            return result;
        }
        return ioctl(proc->iotab[fd],op,(void *)arg_uma);

    case IOC_SETEND:
    // arg is unsigned long long *
        result = enforce_vptr((void *)arg_uma, sizeof(unsigned long long),PTE_U|PTE_R );
        if(result!=0){
            return result;
        }
        return ioctl(proc->iotab[fd],op,(void *)arg_uma);
    case IOC_GETPOS:
    // arg is unsigned long long *
        result = enforce_vptr((void *)arg_uma, sizeof(unsigned long long),PTE_U|PTE_W );
        if(result!=0){
            return result;
        }
        return ioctl(proc->iotab[fd],op,(void *)arg_uma);
    case IOC_SETPOS:
    // arg is const unsigned long long 
        result = enforce_vptr((void *)arg_uma, sizeof(unsigned long long),PTE_U|PTE_R );
        if(result!=0){
            return result;
        }
        return ioctl(proc->iotab[fd],op,(void *)arg_uma); 
    case IOC_MAPBUF:
         result = enforce_vptr((void *)arg_uma, sizeof(void*),PTE_U);
        if(result!=0){
            return result;
        }
        return ioctl(proc->iotab[fd],op,(void *)arg_uma);
    default:
        return ioctl_u(proc->iotab[fd],op,arg_uma);
    }
}

int syspipe(int * wfdptr, int * rfdptr) {
    // YOUR CODE HERE
    int result;
    struct process * proc =current_process();

    //validate both pointer points to user accessible integer 
    result= enforce_vptr((void *)wfdptr,sizeof(int),PTE_U);
    if(result!=0){
        return result;
    }
    result= enforce_vptr((void *)rfdptr,sizeof(int),PTE_U);
    if(result!=0){
        return result;
    }
    int w_val=*wfdptr;
    int r_val=*rfdptr;
    int w_slot=-1;
    int r_slot=-1;
    // two file fd shouldnt be equal if positive;
    if(w_val>=0 && w_val==r_val){
        return -EINVAL;
    }
    if(w_val>=0){
        if(w_val>=PROC_IOMAX){
            return -EBADF;
        }
        if(proc->iotab[w_val]!=NULL){
            return -EMFILE;
        }
        w_slot=w_val;
    }else{
        for(int i=0;i<PROC_IOMAX;i++){
            if(i!=r_val && proc->iotab[i]==NULL){
                w_slot=i;
                break;
            }
        }
        if(w_slot==-1){
            return -EMFILE;
        }
    }
    if(r_val>=0){
        if(r_val>=PROC_IOMAX){
            return -EBADF;
        }
        if(proc->iotab[r_val]!=NULL){
            return -EMFILE;
        }
        r_slot=r_val;
    }else{
        for(int i=0;i<PROC_IOMAX;i++){
            if(i!=w_slot&& proc->iotab[i]==NULL){
                r_slot=i;
                break;
            }
        }
        if(r_slot==-1){
            return -EMFILE;
        }
    }
    create_iopipe(&proc->iotab[w_slot],&proc->iotab[r_slot]);
    *wfdptr=w_slot;
    *rfdptr=r_slot;
    return 0;
}

int sysiodup(int oldfd, int newfd) {
    // YOUR CODE HERE
    int result;
    struct process * proc =current_process();
    // check for old fd validity
    if(oldfd<0|| oldfd>=PROC_IOMAX || proc->iotab[oldfd]==NULL){
        return -EBADF;
    }
    //check for new fd validity and find one if it is negative
    int new_slot=-1;
    if(newfd>=0){
        if( newfd>=PROC_IOMAX || proc->iotab[newfd]!=NULL){
            return -EBADF;
        }
        new_slot=newfd;
    }else {
        for(int i=0;i<PROC_IOMAX;i++){
            if(proc->iotab[i]==NULL){
                new_slot=i;
                break;
            }
        }
        // all io busy
        if(new_slot==-1){
            return -EMFILE;
        }
    }
    proc->iotab[new_slot]=ioaddref(proc->iotab[oldfd]);
    return new_slot;


}
static int age_bucket=0;
static int age_set=0;

int sysab1043(unsigned long long dob){
    // YOUR CODE HERE (CP3)
    struct io* rtcio;
    int result;
    unsigned long long time;
    static unsigned long long udob=dob;
    if(age_set==1){
        return age_bucket;
    }
    result=open_device("rtc",&rtcio);
    if(result<0){
        return result;
    }
    result = ioread(rtcio,&time,sizeof(time));
    //rtc open addref to it, RAII control
    iodropref(rtcio);
    if(result<0){
        return result;
    }
    age_set=1;
    long age=DAYS_TO_YR_APPX(TICK_TO_DAYS(time-udob));
    if(age<13){
        age_bucket=0;
    }else if(age>=13 && age<16){
        age_bucket=1;
    }else if(age>=16 && age<18){
        age_bucket=2;
    }else{
        age_bucket=3;
    }
    return age_bucket;
}

