// plic.c - Interface to RISC-V PLIC
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef PLIC_TRACE
#define TRACE
#endif

#ifdef PLIC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "plic.h"
#include "misc.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

#define PLIC_SRC_CNT 96  // QEMU VIRT_IRQCHIP_NUM_SOURCES
#define PLIC_CTX_CNT 2

// INTERNAL MACRO DEFINITIONS
//

// CTX(i,0) is hartid /i/ M-mode context
// CTX(i,1) is hartid /i/ S-mode context

#define CTX(i,s) (2*(i)+(s))

// INTERNAL TYPE DEFINITIONS
// 

struct plic_regs {
	union {
		uint32_t priority[PLIC_SRC_CNT]; // Interrupt Priorities registers
		char _reserved_priority[0x1000];
	};

	union {
		uint32_t pending[PLIC_SRC_CNT/32]; // Interrupt Pending Bits registers
		char _reserved_pending[0x1000];
	};

	union {
		uint32_t enable[PLIC_CTX_CNT][32]; // Interrupt Enables registers
		char _reserved_enable[0x200000-0x2000];
	};

	struct {
		union {
			struct {
				uint32_t threshold;	// Priority Thresholds registers
				uint32_t claim;	// Interrupt Claim/Completion registers
			};
			
			char _reserved_ctxctl[0x1000];
		};
	} ctx[PLIC_CTX_CNT];
};

// INTERNAL FUNCTION DECLARATIONS
//

static inline void plic_set_source_priority (
	uint32_t srcno, uint32_t level);

static inline int plic_source_pending(uint32_t srcno);

static inline void plic_enable_source_for_context (
	uint32_t ctxno, uint32_t srcno);

static inline void plic_disable_source_for_context (
	uint32_t ctxno, uint32_t srcno);

static inline void plic_set_context_threshold (
	uint32_t ctxno, uint32_t level);

static inline uint32_t plic_claim_context_interrupt (
	uint32_t ctxno);

static inline void plic_complete_context_interrupt (
	uint32_t ctxno, uint32_t srcno);

static void plic_enable_all_sources_for_context(uint32_t ctxno);
static void plic_disable_all_sources_for_context(uint32_t ctxno);

// EXPORTED GLOBAL VARIABLES
//

char plic_initialized = 0;

// We currently only support single-hart operation, sending interrupts to S mode
// on hart 0 (context 0). The low-level PLIC functions already understand
// contexts, so we only need to modify the high-level functions (plic_init,
// plic_claim_request, plic_finish_request)to add support for multiple harts.

// INTERNAL GLOBAL VARIABLES
//

static struct plic_regs * plic;

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void * mmio_base) {
	int i;

	plic = mmio_base;

	// Disable all sources by setting priority to 0

	for (i = 1; i < PLIC_SRC_CNT; i++)
		plic_set_source_priority(i, 0);
	
	// Route all sources to S mode on hart 0 only

	for (int i = 0; i < PLIC_CTX_CNT; i++)
		plic_disable_all_sources_for_context(i);
	
	plic_enable_all_sources_for_context(CTX(0,1));
	plic_set_context_threshold(CTX(0,1), 0);
	plic_initialized = 1;
}

void plic_enable_source(int srcno, int prio) {
	trace("%s(srcno=%d,prio=%d)", __func__, srcno, prio);
	assert (0 < srcno && srcno <= PLIC_SRC_CNT);
	assert (prio > 0);

	plic_set_source_priority(srcno, prio);
}

void plic_disable_source(int srcno) {
	trace("%s()", __func__);
	assert (0 < srcno && srcno <= PLIC_SRC_CNT);

	plic_set_source_priority(srcno, 0);
}

int plic_claim_interrupt(void) {
	trace("%s()", __func__);
	return plic_claim_context_interrupt(CTX(0,1));
}

void plic_finish_interrupt(int irqno) {
	trace("%s(irqno=%d)", __func__, irqno);
	plic_complete_context_interrupt(CTX(0,1), irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

static inline void plic_set_source_priority(uint32_t srcno, uint32_t level) {
	// YOUR CODE HERE
}

static inline int plic_source_pending(uint32_t srcno) {
	// YOUR CODE HERE
}

static inline void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
	// YOUR CODE HERE
}

static inline void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno) {
	// YOUR CODE HERE
}

static inline void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
	// YOUR CODE HERE
}

static inline uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
	// YOUR CODE HERE
}

static inline void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
	// YOUR CODE HERE
}

static void plic_enable_all_sources_for_context(uint32_t ctxno) {
	// YOUR CODE HERE
}

static void plic_disable_all_sources_for_context(uint32_t ctxno) {
	// YOUR CODE HERE
}
