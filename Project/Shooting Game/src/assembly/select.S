.section .data
easy_str:     .asciz "EASY"
normal_str:   .asciz "NORMAL" 
hard_str:     .asciz "HARD"
cursor_str:   .asciz ">"
space_str:    .asciz " "

.section .text
.globl select
.type select, @function
select:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)     // current selection
    sw s1, 4(sp)     // previous button state
    sw s2, 0(sp)     // previous cursor position
    
    li s0, 0         // start with easy mode selected
    li s1, 0         // no previous button press
    li s2, -1        // previous cursor position
    
    // Clear screen
    li a0, 0         // BLACK color
    jal LCD_Clear
    
    // Test: Draw just one string first
    li a0, 20        // x
    li a1, 25        // y
    la a2, easy_str  // "EASY"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    // Test: Force draw cursor at fixed position
    li a0, 10        // x position for cursor
    li a1, 25        // y position for EASY
    la a2, cursor_str // ">"
    li a3, 65535     // WHITE color
    jal LCD_ShowString
    
selection_loop:
    // Simple button test - just check CENTER to exit
    li a0, 4         // JOY_CTR
    jal Get_Button
    bnez a0, cleanup_and_return
    
    // Just loop forever until CENTER is pressed
    j selection_loop
    
cleanup_and_return:
    // Return 0 (EASY mode)
    li a0, 0
    
    // Restore registers and return
    lw s2, 0(sp)
    lw s1, 4(sp)
    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret