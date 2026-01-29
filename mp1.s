#/* @file mp1.s
# * @brief Student implementation of MP1
# * @copyright Copyright (c) 2025-2026 University of Illinois
# * @license SPDX-License-Identifier: NCSA
# */

# See definitions in `skyline.h'
	.extern skyline_star_list       # Head node for the linked list of stars
	.extern skyline_windows         # Array head location of Windows
	.extern skyline_win_cnt			# number of windows
	.text

# Here's how to declare a function in RISC-V assembly
# We suggest looking at the definitions of certain GNU Assembler Directives 
	.global add_star
	.type 	add_star, @function 
# void add_star(int32_t x, int32_t y, uint32_t color)
# Inputs: int32_t x, x coordinate of the star 
#		 int32_t y, y coordinate of the star 
#		 uint32_t color, color of the star
# Outputs: None
# Description: Add a star with specified x, y coordinates and specified color to the skyline star list 
# Side Effects: Dynamically allocate space for a new star and the skyline_star_list is now started with this new star 
add_star:
# callee side convention on entrance
	addi sp,sp, -32 # 32 byte because we need to store a0-a2 and stack pointer is 16 byte aligned
	sd fp,0(sp)
	sd ra, 8(sp)
	sw a0, 16(sp)
	sw a1, 20(sp)
	sw a2, 24(sp)
	addi fp, sp, 32

	#function body 
	li a0, 24 #skyline_star's struct is 24 byte for alignment 
	call kmalloc
	beqz a0, fail_add_star
	mv t0, a0 # the pointer to the created memory block stored in t0
	lw a0, 16(sp)
	lw a1, 20(sp)
	lw a2, 24(sp)
	# we choose to insert the new star at the start of the list to achieve O(1) operation
	la t1, skyline_star_list # get the address of the list start
	ld t2, 0(t1)  # load the pointer to the start star of th elist
	sd t2, 0(t0) # the next pointer of the new star is the old start of the star list
	sw a0, 8(t0) 
	sw a1, 12(t0)
	sw a2, 16(t0)
	sd t0, 0(t1)# the pointer to the star list updated to the new star

# callee side convention before return 
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 32
	ret
fail_add_star:
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 32
	ret

	.global remove_star
	.type 	remove_star, @function 
# void remove_star(int32_t x, int32_t y)
# Inputs: int32_t x, x coordinate of the star to be removed
#		  int32_t y, y coordinate of the star to be removed
# Outputs: None
# Description: Remove the star at location (x,y) from the star list if it exists
# Side Effects: Change the star list if a matching star exists, Frees the memory allocated to this star
remove_star:
	# callee side convention on entrance
	addi sp,sp, -16
	sd fp,0(sp)
	sd ra, 8(sp)
	addi fp, sp, 16
	li t4, 0 #count if the node to be removed is the head
	#function body 
	la t0, skyline_star_list # address of star list start saved to t0
	ld t1, 0(t0)# now t1 contains the address of the first star
	beqz t1, fail_remove_star
while:
	mv t0, t1
	ld t1, 0(t0) #pointer to next star saved at t1 
	lw t2, 8(t0)
	lw t3, 12(t0)
	bne t2, a0, if_not_equal
	bne t3, a1, if_not_equal
	j contd # if both x and y matched, jump to contd
if_not_equal:
	add t4, t4, 1 #means the the node to be removed is not head
	mv t5, t0 # save the previous node
	mv t0,t1 #search for next star
	beqz t0, fail_remove_star #if next star is NULL, return without doing anything
	j while
contd:
	mv t6, t0 #save the node to be deleted at t6
	beqz t4, is_head # jump to is_head if count a4 is 0
	# if not head, replace the next value of previous node to the next node of current one 
	sd t1, 0(t5)
	j is_not_head
is_head:
	# update the value stored in skyline_star_list
	la t0, skyline_star_list
	sd t1, 0(t0)
is_not_head:
	mv a0, t6 # put in the address of the star to be deleted
	call kfree
	# callee side convention before return 	
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret
fail_remove_star:
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret 


	.global	draw_star
	.type draw_star, @function 
# void draw_star(uint32_t * fbuf, const struct skyline_star * star)
# Inputs: uint32_t * fbuf, the pointer that points to buffer waiting for drawing
# 		 const struct skyline_star * star, the pointer that points to the star waiting to be drawed into buffer
# Outputs: None
# Description: Put the given star into the fbuf if that star is within the boundary,return immediatley if any pointer is null
# Side Effects: Change the content of the buffer with that star's color at that star's location 
draw_star:
	# callee side convention on entrance
	addi sp,sp, -16
	sd fp,0(sp)
	sd ra, 8(sp)
	addi fp, sp, 16
	#function body 
	beqz a0, fail_draw_star #check for null
	beqz a1, fail_draw_star
	li t0, 640 # check for boundary
	li t1, 480 
	lw t2, 8(a1) #get x
	lw t3, 12(a1) #get y
	bge t2,t0, fail_draw_star
	bge t3,t1, fail_draw_star
	blt t2,x0, fail_draw_star
	blt t3,x0, fail_draw_star
	# boundary check and null check passed start writing value 
	mul t1, t0, t3 
	add t0, t1, t2
	li t1, 4
	mul t2, t1,t0 # memory offset = (x+640*y)*4 stored in t2
	add t1,a0,t2 # now t1 contains the memory address of the node to be written 
	lw t2, 16(a1) # get color 
	sw t2, 0(t1)# write color
	# callee side convention before return 	
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret
fail_draw_star:
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret

	.global add_window
	.type   add_window, @function
# void add_window (int32_t x, int32_t y, uint8_t w, uint8_t h, uint32_t color)
# Inputs: int32_t x,  x coordinate of the top left corner of the window 
#		  int32_t y,  y coordinate of the top left corner of the window 
#    	  uint8_t w,  width of the window
#		  uint8_t h,  height of the window
#    	  uint32_t color, color of the window
# Outputs: None
# Description: Add a window specified size, color, top left corner to the skyline_windows array
# Side Effects: Add a window to the skyline_windows array at end and add skyline_win_cnt by 1
add_window:
	# callee side convention on entrance
	addi sp,sp, -16
	sd fp,0(sp)
	sd ra, 8(sp)
	addi fp, sp, 16

	#function body 
	la t0, skyline_win_cnt
	lhu t1, 0(t0) # stores the number of windows in t1
	li t2, 4000 # t2 is the maximum number of windows 
	beq t1,t2,fail_add_window # if no room for new window, failed 
	li t2, 16 #struct skyline_window is 16 byte for alignment 
	mul t3,t1,t2 # stores the memory offset at t3
	la t4, skyline_windows # address of the start of they window Array
	add t5,t3,t4  # final space to be written at t5
	sw a0, 0(t5)
	sw a1, 4(t5)
	sb a2, 8(t5)
	sb a3, 9(t5)
	sw a4, 12(t5)
	addi t1,t1,1 # add win_cnt by 1
	sh t1, 0(t0)# update win_cnt
	# callee side convention before return 	
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret
fail_add_window:
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret

	.global remove_window
	.type   remove_window, @function
# void remove_window(int32_t x, int32_t y)
# Inputs: int32_t x, x coordinate of the top left corner of the window to be removed
#		  int32_t y, y coordinate of the top left corner of the window to be removed
# Outputs: None
# Description: Remove the window with top left corner at location (x,y) from the window array if it exists and keep the array contiguous
# Side Effects: Remove a window from the window array and decrease the skyline_win_cnt by 1
remove_window:
	# callee side convention on entrance
	addi sp,sp, -16
	sd fp,0(sp)
	sd ra, 8(sp)
	addi fp, sp, 16
	#function body 
	la t0, skyline_win_cnt
	lhu t1, 0(t0) #stores the number of windows in t1
	la t2, skyline_windows # address of the start of they window Array in t2
	li t3, 0 # t3 is a counter for finding the index of the window 
	li t4, 16
while_window:
	bgeu t3,t1,fail_remove_window # if counter reaches number of windows, not found
	mul t0,t3,t4
	add t0,t0,t2
	lw t5, 0(t0) # stores the x of the current window into t5 
	lw t6, 4(t0)# stores the y into t6
	bne a0,t5,not_equal_window
	bne a1,t6,not_equal_window
	j contd_remove_window
not_equal_window:
	addi t3,t3,1 # add counter by 1
	j while_window
contd_remove_window:
	# to make windows contiguous we replace the window to be deleted with the window at the end
	addi t1,t1,-1# adjust the number for index of the window at end 
	mul t3,t1,t4
	add t3,t3,t2 # now t3 points to the start of the final window
	# use final window value to cover the one that is going to be deleted 
	lw t4, 0(t3) 
	sw t4, 0(t0)
	lw t4, 4(t3) 
	sw t4, 4(t0)
	lbu t4, 8(t3) 
	sb t4, 8(t0)
	lbu t4, 9(t3) 
	sb t4, 9(t0)
	lw t4, 12(t3) 
	sw t4, 12(t0)
	la t0, skyline_win_cnt
	sh t1, 0(t0)# update win_cnt
	# callee side convention before return 	
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret
fail_remove_window:
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret



	.global draw_window
	.type draw_window, @function
# void draw_window(uint32_t * fbuf, const struct skyline_window * win)
# Inputs: uint32_t * fbuf, the pointer that points to buffer waiting for drawing
# 		 const struct skyline_window * win, the pointer that points to the window waiting to be drawed into buffer
# Outputs: None
# Description: Put the given window into the fbuf for parts of that window within the boundaries, return immediatley if any pointer is null
# Side Effects: Change the content of the buffer with that windows color at positions covered by that window
draw_window:
# callee side convention on entrance
	addi sp,sp, -16
	sd fp,0(sp)
	sd ra, 8(sp)
	addi fp, sp, 16
# function body 
	beqz a0, fail_draw_window # check for null
	beqz a1, fail_draw_window
	lw t0, 0(a1) #get x
	lw t1, 4(a1) #get y
	lbu t2, 8(a1) #get w
	lbu t3, 9(a1) #get h
	lw t4, 12(a1) #get color
	li t5, 640 # x border
	li t6, 480 # y border
	# boundary check 
	bge t0,t5,fail_draw_window
	bge t1,t6,fail_draw_window
	add t2,t2,t0 # t2 is now window's right boundary
	add t3,t3,t1 # t3 is now window's lower boundary
	blt t2,x0,fail_draw_window
	blt t3,x0,fail_draw_window
	# passed boundary tests above means that at least some part the window could be on the buffer
	#adjusting 4 boundaries of the window
	blt t0,x0,left_bound_fix # window starting position 
left_bound_fixed:
	bge t2,t5, right_bound_fix
right_bound_fixed:
	blt t1,x0,upper_bound_fix
upper_bound_fixed:
	bge t3,t6,lower_bound_fix
lower_bound_fixed:
	j allboundary_fixed

right_bound_fix:
	li t2,640 
	j right_bound_fixed
left_bound_fix:
	li t0,0 
	j left_bound_fixed
upper_bound_fix:
	li t1,0
	j upper_bound_fixed
lower_bound_fix:
	li t3, 480
	j lower_bound_fixed
allboundary_fixed:
	mv t5, t0
	mv t6, t1 #now t5 and t6 set to x and y counter 
while_drawing_window:
	li a2, 640 #memory offset = (x+640*y)*4 
	mul a2,a2,t6
	add a2,a2,t5
	li a3,4
	mul a2,a2,a3 
	add a2,a2,a0
	sw t4,0(a2)
	addi t5,t5,1
	bge t5,t2,next_line # if x passed right_boundary went to next_line
	j while_drawing_window
next_line:
	mv t5, t0 # reset x counter to left boundary 
	addi t6,t6,1
	bge t6,t3, finished# if y passed lower boundary, finsished coloring
	j while_drawing_window
finished:
# callee side convention before return 	
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret
fail_draw_window:
	ld fp, 0(sp)
	ld ra, 8(sp)
	addi sp, sp, 16
	ret



	.global start_beacon
	.type start_beacon, @function

# void void start_beacon ( const uint32_t * img, int32_t x, int32_t y, uint8_t dia, uint16_t period, uint16_t ontime)
# Inputs:const uint32_t * img, image of the beacon 
#				int32_t x, x coordinate of the top left corner of the beacon 
#				int32_t y, y coordinate of the top left corner of the beacon 
#				uint8_t dia, diameter of the beacon 
#				uint16_t period, period of the beacon 
#				uint16_t ontime, ontime of the beacon 
# Outputs: None
# Description: initializes all fields of the /skyline_beacon/ struct with the given values
# Side Effects: initializes skyline_beacon
start_beacon:
	la t0, skyline_beacon

	sd a0, 0(t0)
	sw a1, 8(t0)
	sw a2, 12(t0)
	sb a3, 16(t0)
	sh a4, 18(t0)
	sh a5, 20(t0)

	ret

	.global draw_beacon
	.type draw_beacon, @function
# void draw_beacon(uint32_t * fbuf,uint64_t t, const struct skyline_beacon * bcn)
# Inputs: uint32_t * fbuf, the pointer that points to buffer waiting for drawing
#		  uint64_t t, current time
# 		 const struct skyline_beacon * bcn, the pointer that points to the beacon waiting to be drawed into buffer
# Outputs: None
# Description: Draw the beacon on the widnow  if the current time% period is smaller than on time, return immediatley if any pointer is null
# Side Effects: Change the content of the buffer with that beacon's img at positions covered by that beacon 
draw_beacon:
	# callee side convention on entrance
	addi sp,sp, -32 # I need to store s1 and s2 for offset if x and y are negative
	sd fp,0(sp)
	sd ra, 8(sp)
	sd s1, 16(sp)
	sd s2, 24(sp)
	addi fp, sp, 32
	# function body
	beqz a0, fail_draw_beacon # check for null
	beqz a2, fail_draw_beacon
	ld t0, 0(a2) #image
	lw t1, 8(a2) #x
	lw t2, 12(a2) #y
	lbu t3, 16(a2) #diameter
	lhu t4, 18(a2) #period
	lhu t5, 20(a2) #ontime
	rem t6,a1,t4 # t6= time%period
	bgeu t6,t5, fail_draw_beacon # if t6 is larger than or equal to ontime, return without drawing 
	li a3, 640
	li a4, 480 # boundary check
	bge t1,a3,fail_draw_beacon
	bge t2,a4,fail_draw_beacon
	add t4,t1,t3 # t4 is now right boundary
	add t5,t2,t3 #t5 is now lower boundary 
	blt t4,x0,fail_draw_beacon
	blt t5,x0,fail_draw_beacon
# passed boundary passes above means at least some part of beacon on window
	# adjusting 4 boundaries of the window
	mv s1,t1 # save original x and y for later offset calculation in case x and y are negative
	mv s2,t2
	blt t1,x0,left_bound_fix2 # window starting position 
left_bound_fixed2:
	bge t4,a3, right_bound_fix2
right_bound_fixed2:
	blt t2,x0,upper_bound_fix2
upper_bound_fixed2:
	bge t5,a4,lower_bound_fix2
lower_bound_fixed2:
	j allboundary_fixed2

right_bound_fix2:
	li t4,640 # boundaries are exclusive
	j right_bound_fixed2
left_bound_fix2:
	li t1,0 
	j left_bound_fixed2
upper_bound_fix2:
	li t2,0
	j upper_bound_fixed2
lower_bound_fix2:
	li t5, 480
	j lower_bound_fixed2
allboundary_fixed2:
	sub s1, x0,s1 # s1 is negative of original x , means the offset
	sub s2, x0,s2 # s2 is negative of original y
	bge s1,x0, img_x_offset # if negative of x is postive, it means there is an x offset, keeps it
	li s1,0 # if no offset, s1=0
img_x_offset:
	bge s2,x0,img_y_offset #if negative of y is postive, it means there is an y offset, keeps it
	li s2,0 # otherwise s2 = 0
img_y_offset:
	mv a3, t1
	mv a4, t2 # now a3 and a4 set to x and y counter 
while_drawing_beacon:
	sub a5, a3,t1
	add a5,a5,s1 # add x offset to image x
	sub a6, a4,t2 
	add a6,a6,s2 # add y offset to image y
	mul a7,t3,a6 
	add a7,a7,a5
	li a5,4
	mul a7,a7,a5 # memory offset in the image = (y*diameter+x)*4
	add a7,a7,t0
	lw a7, 0(a7) # color stored in a7
	li t6,640
	mul t6,t6,a4
	add t6,t6,a3
	li a5,4
	mul t6,t6,a5
	add t6,t6,a0
	sw a7,0(t6)
	addi a3,a3,1
	bge a3,t4,next_line_drawing_beacon
	j while_drawing_beacon
next_line_drawing_beacon:
	mv a3,t1
	addi a4,a4,1
	bge a4,t5,finished_beacon_drawing
	j while_drawing_beacon
finished_beacon_drawing:
	# callee side convention before return 	
	ld fp, 0(sp)
	ld ra, 8(sp)
	ld s1, 16(sp)
	ld s2, 24(sp)
	addi sp, sp, 32
	ret
fail_draw_beacon:
	ld fp, 0(sp)
	ld ra, 8(sp)
	ld s1, 16(sp)
	ld s2, 24(sp)
	addi sp, sp, 32
	ret
.end
