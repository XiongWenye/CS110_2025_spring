.section .data
title_char1:    .asciz "SUP"
title_char2:    .asciz "ERC"
title_char3:    .asciz "ELL"
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
    sw s0, 8(sp)     // current selection (0=easy, 1=normal, 2=hard)
    sw s1, 4(sp)     // previous button state
    sw s2, 0(sp)     // previous cursor position
    
    li s0, 0         // start with easy mode selected
    li s1, 0         // no previous button press
    li s2, -1        // previous cursor position
    
    // Clear screen
    li a0, 0         // BLACK color
    jal LCD_Clear
    // Draw title
    li a0, 100        // x - centered position
    li a1, 20         // y - top of screen
    la a2, title_char1  // 
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    li a0, 100        // x
    li a1, 40         // y
    la a2, title_char2  // 
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    li a0, 100        // x
    li a1, 60         // y
    la a2, title_char3  // 
    li a3, 65535     // WHITE
    jal LCD_ShowString

    // Draw initial options - all white first
    jal draw_all_options_white
    
    // Draw initial cursor and highlight first option
    jal draw_cursor_and_highlight
    
    mv s2, s0        // Update previous cursor position to current
    
selection_loop:
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
    j redraw_cursor
    
wrap_to_bottom:
    li s0, 2         // wrap to hard mode
    j redraw_cursor
    
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
    j redraw_cursor
    
wrap_to_top:
    li s0, 0         // wrap to easy mode
    j redraw_cursor
    
redraw_cursor:
    // Restore old option to white color
    jal restore_old_option_white
    
    // Draw cursor and highlight new option
    jal draw_cursor_and_highlight
    
    mv s2, s0        // Update previous cursor position
    
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

draw_all_options_white:
    // Draw EASY in white
    li a0, 20        // x
    li a1, 25        // y
    la a2, easy_str  // "EASY"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    // Draw NORMAL in white
    li a0, 20        // x
    li a1, 40        // y
    la a2, normal_str // "NORMAL"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    // Draw HARD in white
    li a0, 20        // x
    li a1, 55        // y
    la a2, hard_str  // "HARD"
    li a3, 65535     // WHITE
    jal LCD_ShowString
    
    ret

restore_old_option_white:
    // Only restore if s2 is valid (not -1)
    li t0, -1
    beq s2, t0, skip_restore
    
    li a0, 20        // x position for options
    
    // Calculate old option Y position and string
    beqz s2, restore_easy     // if s2 == 0 (EASY)
    li t0, 1
    beq s2, t0, restore_normal // if s2 == 1 (NORMAL)
    // s2 == 2 (HARD)
    li a1, 55        // HARD Y position
    la a2, hard_str  // "HARD"
    j restore_option
    
restore_easy:
    li a1, 25        // EASY Y position
    la a2, easy_str  // "EASY"
    j restore_option
    
restore_normal:
    li a1, 40        // NORMAL Y position
    la a2, normal_str // "NORMAL"
    
restore_option:
    li a3, 65535     // WHITE color
    jal LCD_ShowString
    
skip_restore:
    ret

draw_cursor_and_highlight:
    // Erase old cursor position first
    li a0, 10        // x position for cursor
    li a1, 25        // EASY Y position
    la a2, space_str // " " to erase
    li a3, 0         // BLACK color
    jal LCD_ShowString
    
    li a0, 10        // x position for cursor
    li a1, 40        // NORMAL Y position
    la a2, space_str // " " to erase
    li a3, 0         // BLACK color
    jal LCD_ShowString
    
    li a0, 10        // x position for cursor
    li a1, 55        // HARD Y position
    la a2, space_str // " " to erase
    li a3, 0         // BLACK color
    jal LCD_ShowString
    
    // Draw cursor at new position
    li a0, 10        // x position for cursor
    
    // Calculate new cursor Y position and highlight option
    beqz s0, highlight_easy     // if s0 == 0 (EASY)
    li t0, 1
    beq s0, t0, highlight_normal // if s0 == 1 (NORMAL)
    // s0 == 2 (HARD)
    li a1, 55        // HARD Y position
    la a2, cursor_str // ">"
    li a3, 65535     // WHITE color
    jal LCD_ShowString
    
    // Highlight HARD option in blue
    li a0, 20        // x position for options
    li a1, 55        // HARD Y position
    la a2, hard_str  // "HARD"
    li a3, 31        // BLUE color
    jal LCD_ShowString
    j done_highlight
    
highlight_easy:
    li a1, 25        // EASY Y position
    la a2, cursor_str // ">"
    li a3, 65535     // WHITE color
    jal LCD_ShowString
    
    // Highlight EASY option in blue
    li a0, 20        // x position for options
    li a1, 25        // EASY Y position
    la a2, easy_str  // "EASY"
    li a3, 31        // BLUE color
    jal LCD_ShowString
    j done_highlight
    
highlight_normal:
    li a1, 40        // NORMAL Y position
    la a2, cursor_str // ">"
    li a3, 65535     // WHITE color
    jal LCD_ShowString
    
    // Highlight NORMAL option in blue
    li a0, 20        // x position for options
    li a1, 40        // NORMAL Y position
    la a2, normal_str // "NORMAL"
    li a3, 31        // BLUE color
    jal LCD_ShowString
    
done_highlight:
    ret
    
cleanup_and_return:
    // Restore registers and return
    lw s2, 0(sp)
    lw s1, 4(sp)
    lw s0, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    ret