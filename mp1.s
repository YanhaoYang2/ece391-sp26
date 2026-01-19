#/* @file mp1.s
# * @brief Student implementation of MP1
# * @copyright Copyright (c) 2025-2026 University of Illinois
# * @license SPDX-License-Identifier: NCSA
# */

# See definitions in `skyline.h'
	.extern skyline_star_list       # Head node for the linked list of stars
	.extern skyline_windows         # Array head location of Windows

	.text

# Here's how to declare a function in RISC-V assembly
# We suggest looking at the definitions of certain GNU Assembler Directives 

	.global add_window
	.type   add_window, @function
add_window:
	ret


	.global start_beacon
	.type start_beacon, @function


start_beacon:
	la t0, skyline_beacon

	sd a0, 0(t0)
	sw a1, 8(t0)
	sw a2, 12(t0)
	sb a3, 16(t0)
	sh a4, 18(t0)
	sh a5, 20(t0)

	ret

.end
