.data
test_input: .word 6 3 5 12

.text
main:
	add t0, x0, x0
	addi t1, x0, 4
	la t2, test_input
main_loop:
	beq t0, t1, main_exit
	slli t3, t0, 2
	add t4, t2, t3
	lw a0, 0(t4)

	addi sp, sp, -20
	sw t0, 0(sp)
	sw t1, 4(sp)
	sw t2, 8(sp)
	sw t3, 12(sp)
	sw t4, 16(sp)

	jal ra, seriesOp

	lw t0, 0(sp)
	lw t1, 4(sp)
	lw t2, 8(sp)
	lw t3, 12(sp)
	lw t4, 16(sp)
	addi sp, sp, 20

	addi a1, a0, 0
	addi a0, x0, 1
	ecall # Print Result
	addi a1, x0, ' '
	addi a0, x0, 11
	ecall
	
	addi t0, t0, 1
	jal x0, main_loop
main_exit:  
	addi a0, x0, 10
	ecall # Exit

seriesOp:
    # a0 contains n (input parameter)
    addi t0, a0, 0     # t0 = n (counter)
    addi t1, x0, 0     # t1 = result accumulator
    addi t2, x0, 1     # t2 = sign flag (1 for positive, 0 for negative)
    
loop:
    beq t0, x0, done   # Exit when counter reaches 0
    
    # Check if sign is positive
    beq t2, x0, negative
    
    # Sign is positive, add counter to result
    add t1, t1, t0
    j flip_sign
    
negative:
    # Sign is negative, subtract counter from result
    sub t1, t1, t0
    
flip_sign:
    # Flip the sign (0 to 1, 1 to 0)
    xori t2, t2, 1
    
    # Decrement counter
    addi t0, t0, -1
    
    j loop
    
done:
    addi a0, t1, 0     # Store result in a0 for return
    jr ra              # Return to caller