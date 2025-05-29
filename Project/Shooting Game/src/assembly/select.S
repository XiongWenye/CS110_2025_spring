.section .data
title_char1:    .asciz "席"
title_char2:    .asciz "文"
title_char3:    .asciz "义"
easy_str:     .asciz "EASY"
normal_str:   .asciz "NORMAL" 
hard_str:     .asciz "HARD"
cursor_str:   .asciz ">"
fps_str:      .asciz "FPS:"
space_str:    .asciz "   "  // 3 spaces to clear old FPS numbers

.section .text
.globl select
.type select, @function
select:
    addi sp, sp, -28
    sw ra, 24(sp)
    sw s0, 20(sp)     // current selection (0=easy, 1=normal, 2=hard)
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
    
    // Initialize timer
    jal timer_init
    
    // Get initial time
    jal get_tick_count
    mv s4, a0        // Store initial time
    
    // Initial screen setup (only done once)
    li a0, 0         // BLACK color
    jal LCD_Clear
    
    // Draw static elements (title and options) - only once
    jal draw_static_elements
    
selection_loop:
    // Increment frame counter
    addi s3, s3, 1
    
    // Calculate FPS every 60 frames (about 1 second at 60 FPS)
    li t0, 60
    rem t1, s3, t0
    bnez t1, skip_fps_calculation
    
    // Calculate FPS
    jal get_tick_count
    mv t2, a0        // Current time
    sub t3, t2, s4   // Time elapsed since last FPS calculation (in ms)
    
    // FPS = (frames * 1000) / time_elapsed_ms
    li t4, 60000     // 60 frames * 1000 ms
    beqz t3, skip_fps_calculation  // Avoid division by zero
    div s5, t4, t3   // Calculate FPS
    
    mv s4, t2        // Update last FPS update time
    
    // Clear old FPS display
    li a0, 42        // x (after "FPS:")
    li a1, 100       // y
    la a2, space_str // "   " to clear old numbers
    li a3, 0         // BLACK color
    jal LCD_ShowString
    
    // Display new FPS
    li a0, 42        // x (after "FPS:")
    li a1, 100       // y
    mv a2, s5        // FPS value
    li a3, 3         // 3 digits
    li a4, 65535     // WHITE
    jal LCD_ShowNum
    
skip_fps_calculation:
    // Only redraw cursor if position changed
    bne s0, s2, redraw_cursor
    j check_input
    
redraw_cursor:
    // Erase old cursor position
    beq s2, -1, skip_erase  // Skip if first draw
    li a0, 25        // x position for cursor
    li a1, 40        // base y position
    li t0, 20        // offset per option
    mul t1, s2, t0   // calculate old offset
    add a1, a1, t1   // old y position
    la a2, space_str // " " to erase
    li a3, 0         // BLACK color
    jal LCD_ShowString
    
skip_erase:
    // Draw new cursor
    li a0, 25        // x position for cursor
    li a1, 40        // base y position
    li t0, 20        // offset per option
    mul t1, s0, t0   // calculate new offset
    add a1, a1, t1   // new y position
    la a2, cursor_str // ">"
    li a3, 31        // BLUE color
    jal LCD_ShowString
    
    mv s2, s0        // Update previous cursor position
    
check_input:
    // Check for UP button (move cursor up)
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
    // Check for DOWN button (move cursor down)
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
    // Check for CENTER button (select option)
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
    // Draw title
    li a0, 60        // x position (centered)
    li a1, 10        // y position
    la a2, title_char1 // "席"
    li a3, 0xF800     // RED color
    jal LCD_ShowChinese
    
    li a0, 80        // x position
    li a1, 10        // y position
    la a2, title_char2 // "文"
    li a3, 0xF800
    jal LCD_ShowChinese
    
    li a0, 100       // x position
    li a1, 10        // y position
    la a2, title_char3 // "义"
    li a3, 0xF800
    jal LCD_ShowChinese

    // Draw options
    li a0, 40        // x
    li a1, 15        // y
    la a2, easy_str  // "EASY"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    li a0, 40        // x
    li a1, 35        // y
    la a2, normal_str // "NORMAL"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    li a0, 40        // x
    li a1, 55        // y
    la a2, hard_str  // "HARD"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    // Draw static FPS label
    li a0, 10        // x
    li a1, 75      // y
    la a2, fps_str   // "FPS:"
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