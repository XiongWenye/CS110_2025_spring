.section .data
title_str:    .asciz "SELECT DIFFICULTY"
easy_str:     .asciz "EASY"
normal_str:   .asciz "NORMAL" 
hard_str:     .asciz "HARD"
cursor_str:   .asciz ">"

.section .text
.globl select
.type select, @function
select:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)     // current selection (0=easy, 1=normal, 2=hard)
    sw s1, 4(sp)     // previous button state
    sw s2, 0(sp)     // button debounce counter
    
    li s0, 0         // start with easy mode selected
    li s1, 0         // no previous button press
    li s2, 0         // debounce counter
    
selection_loop:
    // Clear screen
    li a0, 0         // BLACK color
    jal LCD_Clear
    
    // Draw title
    li a0, 20        // x
    li a1, 10        // y  
    la a2, title_str // string
    li a3, 65535     // WHITE color
    jal LCD_ShowString
    
    // Draw options
    li a0, 40        // x
    li a1, 40        // y
    la a2, easy_str  // "EASY"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    li a0, 40        // x
    li a1, 60        // y
    la a2, normal_str // "NORMAL"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    li a0, 40        // x
    li a1, 80        // y
    la a2, hard_str  // "HARD"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    // Draw cursor based on current selection
    li a0, 25        // x position for cursor
    li a1, 40        // base y position
    li t0, 20        // offset per option
    mul t1, s0, t0   // calculate offset
    add a1, a1, t1   // final y position
    la a2, cursor_str // ">"
    li a3, 31        // BLUE color
    jal LCD_ShowString
    
    // Check for UP button (move cursor up)
    li a0, 2         // JOY_UP
    jal Get_Button
    beqz a0, check_down
    
    // Button pressed, check debounce
    bnez s1, check_down  // if previous button was pressed, skip
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
    // Reset button state if no button is pressed
    li a0, 1         // JOY_DOWN
    jal Get_Button
    bnez a0, keep_button_state
    
    li a0, 2         // JOY_UP  
    jal Get_Button
    bnez a0, keep_button_state
    
    li s1, 0         // reset button state
    
keep_button_state:
    // Small delay
    li a0, 50
    jal delay_1ms
    
    j selection_loop
    
cleanup_and_return:
    // Restore registers and return
    lw s2, 0(sp)
    lw s1, 4(sp)
    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret