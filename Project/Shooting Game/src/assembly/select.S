.section .data
title_char3:    .asciz ""
easy_str:     .asciz "EASY"
normal_str:   .asciz "NORMAL" 
hard_str:     .asciz "HARD"
cursor_str:   .asciz ">"
fps_str:      .asciz "FPS:"
space_str:    .asciz " "

.section .text
.globl select
.type select, @function
select:
    addi sp, sp, -28
    sw ra, 24(sp)
    sw s0, 20(sp)     // current selection
    sw s1, 16(sp)     // previous button state
    sw s2, 12(sp)     // previous cursor position
    sw s3, 8(sp)      // frame counter
    sw s4, 4(sp)      // last FPS update time
    sw s5, 0(sp)      // current FPS value
    
    li s0, 0         // start with easy mode selected
    li s1, 0         // no previous button press
    li s2, -1        // previous cursor position (invalid initially)
    li s3, 0         // frame counter
    li s5, 0         // current FPS
    
    // Initial screen setup
    li a0, 0         // BLACK color
    jal LCD_Clear
    
    // Draw static elements
    jal draw_static_elements
    
    // Force initial cursor draw
    j force_cursor_draw
    
selection_loop:
    // Only redraw cursor if position changed
    bne s0, s2, redraw_cursor
    j check_input
    
force_cursor_draw:
redraw_cursor:
    // Erase old cursor position (if not first draw)
    li t2, -1
    beq s2, t2, skip_erase
    li a0, 10        // x position for cursor
    
    // Calculate old cursor Y position
    beqz s2, old_cursor_easy     // if s2 == 0 (EASY)
    li t0, 1
    beq s2, t0, old_cursor_normal // if s2 == 1 (NORMAL)
    li a1, 55        // s2 == 2 (HARD)
    j erase_old_cursor
    
old_cursor_easy:
    li a1, 25        // EASY Y position
    j erase_old_cursor
    
old_cursor_normal:
    li a1, 40        // NORMAL Y position
    
erase_old_cursor:
    la a2, space_str // " " to erase
    li a3, 0         // BLACK color
    jal LCD_ShowString
    
skip_erase:
    // Draw new cursor
    li a0, 10        // x position for cursor
    
    // Calculate new cursor Y position
    beqz s0, new_cursor_easy     // if s0 == 0 (EASY)
    li t0, 1
    beq s0, t0, new_cursor_normal // if s0 == 1 (NORMAL)
    li a1, 55        // s0 == 2 (HARD)
    j draw_new_cursor
    
new_cursor_easy:
    li a1, 25        // EASY Y position
    j draw_new_cursor
    
new_cursor_normal:
    li a1, 40        // NORMAL Y position
    
draw_new_cursor:
    la a2, cursor_str // ">"
    li a3, 31        // BLUE color
    jal LCD_ShowString
    
    mv s2, s0        // Update previous cursor position
    
check_input:
    // Check for UP button
    li a0, 2         // JOY_UP
    jal Get_Button
    beqz a0, check_down
    
    // Button pressed, check debounce
    bnez s1, check_down
    li s1, 1         // mark button as pressed
    
    // Move cursor up
    addi s0, s0, -1
    bltz s0, wrap_to_bottom
    j check_center
    
wrap_to_bottom:
    li s0, 2         // wrap to hard mode
    j check_center
    
check_down:
    // Check for DOWN button
    li a0, 1         // JOY_DOWN
    jal Get_Button
    beqz a0, check_center
    
    // Button pressed, check debounce
    bnez s1, check_center
    li s1, 1         // mark button as pressed
    
    // Move cursor down
    addi s0, s0, 1
    li t0, 3
    bge s0, t0, wrap_to_top
    j check_center
    
wrap_to_top:
    li s0, 0         // wrap to easy mode
    
check_center:
    // Check for CENTER button
    li a0, 4         // JOY_CTR
    jal Get_Button
    beqz a0, reset_button_state
    
    // Center button pressed - return selected mode
    mv a0, s0        // return current selection
    j cleanup_and_return
    
reset_button_state:
    // Reset button state if no buttons are pressed
    li a0, 1         // JOY_DOWN
    jal Get_Button
    bnez a0, keep_button_state
    
    li a0, 2         // JOY_UP  
    jal Get_Button
    bnez a0, keep_button_state
    
    li s1, 0         // reset button state
    
keep_button_state:
    j selection_loop
    
draw_static_elements:
    // Draw options - safe coordinates
    li a0, 20        // x
    li a1, 25        // y
    la a2, easy_str  // "EASY"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    li a0, 20        // x
    li a1, 40        // y
    la a2, normal_str // "NORMAL"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    li a0, 20        // x
    li a1, 55        // y
    la a2, hard_str  // "HARD"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    ret
    
cleanup_and_return:
    // Restore registers and return
    lw s5, 0(sp)
    lw s4, 4(sp)
    lw s3, 8(sp)
    lw s2, 12(sp)
    lw s1, 16(sp)
    lw s0, 20(sp)
    lw ra, 24(sp)
    addi sp, sp, 28
    ret