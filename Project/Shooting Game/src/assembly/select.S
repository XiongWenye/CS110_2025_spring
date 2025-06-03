.section .data
 title_char1:    .asciz "SUP"
 title_char2:    .asciz "ERC"
 title_char3:    .asciz "ELL"
 easy_str:     .asciz "EASY"
 normal_str:   .asciz "NORMAL"
 hard_str:     .asciz "HARD"
 cursor_str:   .asciz ">"
 space_str:    .asciz " "
+
+// It's assumed a C global variable like 'int g_selected_difficulty;' exists.
+// If defined in this assembly file, it would be:
+// .globl g_selected_difficulty
+// .data
+// g_selected_difficulty: .word 0

 .section .text
 .globl select
 .type select, @function
 select:
     addi sp, sp, -16
     sw ra, 12(sp)
     sw s0, 8(sp)     // current selection (0=easy, 1=normal, 2=hard)
     sw s1, 4(sp)     // previous button state
     sw s2, 0(sp)     // previous cursor Y-coordinate for clearing (using actual Y now)

     li s0, 0         // start with easy mode selected (0)
     li s1, 0         // no previous button press
-    li s2, -1        // previous cursor position (using Y-coord directly is better)
+    li s2, 25        // Initial Y position of cursor for "EASY" to clear later

     // Clear screen
     li a0, 0         // BLACK color
     jal LCD_Clear
     // Draw title
     li a0, 100        // x - centered position
     li a1, 20         // y - top of screen
     la a2, title_char1
     li a3, 65535     // WHITE
     jal LCD_ShowString

     li a0, 100        // x
     li a1, 35         // y
     la a2, title_char2
     li a3, 65535     // WHITE
     jal LCD_ShowString

     li a0, 100        // x
     li a1, 50         // y
     la a2, title_char3
     li a3, 65535     // WHITE
     jal LCD_ShowString

     // Draw all options
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

     // Draw initial cursor
     li a0, 10        // x position for cursor
     li a1, 25        // y position for EASY (initial selection)
     la a2, cursor_str // ">"
     li a3, 65535     // WHITE color
     jal LCD_ShowString
-
-    mv s2, s0        // Update previous cursor position to current
-
 selection_loop:
     // Check for UP button
     li a0, 3         // JOY_UP
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
     // Erase old cursor position
     li a0, 10        // x position for cursor
     mv a1, s2        // Use stored Y of previous cursor for a1
     la a2, space_str // " " to erase
     li a3, 0         // BLACK color to erase (actually WHITE text on BLACK background for space)
     jal LCD_ShowString

     // Draw new cursor
     li a0, 10        // x position for cursor

     // Calculate new cursor Y position based on s0 (current selection)
     li t1, 25        // Y for EASY
     li t2, 40        // Y for NORMAL
     li t3, 55        // Y for HARD

     beqz s0, draw_new_cursor_at_y_easy
     li t0, 1
     beq s0, t0, draw_new_cursor_at_y_normal
     mv a1, t3        // s0 == 2 (HARD), Y is 55
     j draw_new_cursor_final

 draw_new_cursor_at_y_easy:
     mv a1, t1        // Y is 25
     j draw_new_cursor_final

 draw_new_cursor_at_y_normal:
     mv a1, t2        // Y is 40
     // j draw_new_cursor_final // Fall through

 draw_new_cursor_final:
     la a2, cursor_str // ">"
     li a3, 65535     // WHITE color
     jal LCD_ShowString

     mv s2, a1        // Update previous cursor Y to current cursor Y

 check_center:
     // Check for CENTER button
     li a0, 4         // JOY_CTR
     jal Get_Button
     beqz a0, reset_button_state

     // Center button pressed - return selected mode
     mv a0, s0        // return current selection in a0 (standard way)
     j cleanup_and_return

 reset_button_state:
     // Reset button state if no directional buttons are pressed
     li a0, 1         // JOY_DOWN
     jal Get_Button
     bnez a0, keep_button_state // If JOY_DOWN is pressed, don't reset s1

     li a0, 3         // JOY_UP
     jal Get_Button
     bnez a0, keep_button_state // If JOY_UP is pressed, don't reset s1

     // Only reset s1 if neither UP nor DOWN is currently held
     // (This handles the case where user presses CTR while still holding UP/DOWN)
     // For a simpler debounce, we might want to check if *any* button is pressed.
     // The original logic might be fine, assuming Get_Button returns 0 if not pressed.
     // Let's refine the button release logic slightly for s1.
     // s1 is a general "a directional button was pressed this cycle" flag.
     // It should be reset if no relevant (UP/DOWN/CTR) button is active.

     li t0, 0 // Assume no buttons active that would prevent s1 reset
     li a0, 3 // JOY_UP
     jal Get_Button
     or t0, t0, a0 // if a0 is non-zero, t0 becomes non-zero

     li a0, 1 // JOY_DOWN
     jal Get_Button
     or t0, t0, a0

     li a0, 4 // JOY_CTR
     jal Get_Button
     or t0, t0, a0

     bnez t0, keep_button_state // If any of these buttons are still pressed, keep s1
     li s1, 0         // reset button state if no relevant buttons are active

 keep_button_state:
     j selection_loop

 cleanup_and_return:
+    // MODIFICATION: Store the result in a global variable
+    // Assumes g_selected_difficulty is a global variable defined in C
+    // or accessible to the linker.
+    la t0, g_selected_difficulty  // Load address of the global variable
+    sw s0, 0(t0)                 // Store the selected difficulty (from s0) into it
+
     // Restore registers and return
     lw s2, 0(sp)
     lw s1, 4(sp)
     lw s0, 8(sp)
     lw ra, 12(sp)
     addi sp, sp, 16
     ret