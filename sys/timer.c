// timer.c - Timer and Alarms
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef TIMER_TRACE
#define TRACE
#endif

#ifdef TIMER_DEBUG
#define DEBUG
#endif

#include "timer.h"
#include "thread.h"
#include "riscv.h"
#include "sbi.h" // for sbi_set_timer
#include "intr.h"
#include "misc.h"
#include "string.h"

#include <stddef.h>
#include <limits.h> // for ULLONG_MAX

// COMPILE-TIME OPTIONS
//

#ifndef BOLT_FREQ
#define BOLT_FREQ 50 // Hz [MP3cp3]
#endif

// INTERNAL TYPE DEFINITIONS
//

struct timer_alarm {
    // YOUR CODE HERE
    struct timer_alarm * next;
    unsigned long long twake;
    int expired;
    struct condition cond;//build of the struct timer alarm
};

// EXPORTED GLOBAL VARIABLES
//

char timer_initialized = 0;
unsigned int timer_frequency = 0;


// INTERNAL GLOBAL VARIABLES
//

static struct timer_alarm * sleep_list; // list of pending alarms
static unsigned long long bolt_interval_ticks;


// INTERNAL FUNCTION DECLARATIONS
//


static void enable_timer_interrupts(void); // sets sie.STIE=1
static void disable_timer_interrupts(void); // clears sie.STIE


// EXPORTED FUNCTION DEFINITIONS
//

void timer_init(unsigned int freq) {
    // YOUR CODE HERE
    timer_frequency = freq;         //record the hardware timer frequency
    sleep_list = NULL;
    bolt_interval_ticks = freq / BOLT_FREQ;
    if (bolt_interval_ticks == 0)
        bolt_interval_ticks = 1;

    sbi_set_timer(rdtime() + bolt_interval_ticks);
    enable_timer_interrupts();
    timer_initialized = 1;
}

void sleep_until(unsigned long long twake) {
    // YOUR CODE HERE
    struct timer_alarm alarm;
    struct timer_alarm ** a_add;

    if (twake <= rdtime()){
        return;
    }              //skip out if the wake time is passed
        

    memset(&alarm, 0, sizeof(alarm));
    alarm.twake = twake;
    condition_init(&alarm.cond, "timer_alarm");

    disable_interrupts();
//insert the alarm into the sleep list
    a_add = &sleep_list;
    while (*a_add != NULL && (*a_add)->twake <= twake){
        a_add = &(*a_add)->next;
    }
    alarm.next = *a_add;
    *a_add = &alarm;

    sbi_set_timer(sleep_list->twake);
    enable_timer_interrupts();
//sleep until the alarm is expired
    while (!alarm.expired){
        condition_wait(&alarm.cond);
    }
    enable_interrupts();
}

void sleep_sec(unsigned int sec) {
    sleep_until(rdtime() + 1ULL * sec * timer_frequency);
}

void sleep_ms(unsigned int ms) {
    sleep_until(rdtime() + 1ULL * ms * timer_frequency / 1000);
}

void sleep_us(unsigned int us) {
    sleep_until(rdtime() + 1ULL * us * timer_frequency / 1000 / 1000);
}

void handle_timer_interrupt(void) {

    unsigned long long tnow = rdtime();
    unsigned long long next_bolt;
    unsigned long long next_alarm;
    unsigned long long next_deadline;

    trace("[%lu] %s()", tnow, __func__);

    // YOUR CODE HERE
    struct timer_alarm * a;
//remove and wake all alarms expired
    while (sleep_list != NULL && sleep_list->twake <= tnow) {
        a = sleep_list;
        sleep_list = a->next;
        a->expired = 1;
        condition_broadcast(&a->cond);
    }

    next_bolt = tnow + bolt_interval_ticks;
    next_alarm = (sleep_list != NULL) ? sleep_list->twake : ULLONG_MAX;
    next_deadline = (next_alarm < next_bolt) ? next_alarm : next_bolt;
    sbi_set_timer(next_deadline);
    enable_timer_interrupts();
}

// INTERNAL FUNCTION DEFINITIONS
//


void enable_timer_interrupts(void) {
    csrs_sie(RISCV_SIE_STIE);
}

void disable_timer_interrupts(void) {
    csrc_sie(RISCV_SIE_STIE);
}