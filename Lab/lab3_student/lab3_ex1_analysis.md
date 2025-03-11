Lab 3 Exercise 1 Analysis

```assembly
.data
n: .word 9

.text
main:   add     t0, x0, x0        # t0 = 0 (first Fibonacci number)
        addi    t1, x0, 1         # t1 = 1 (second Fibonacci number)
        la      t3, n             # load address of n into t3
        lw      t3, 0(t3)         # load value of n into t3 (loop counter)
fib:    beq     t3, x0, finish    # if counter == 0, exit loop
        add     t2, t1, t0        # t2 = t1 + t0 (next Fibonacci number)
        mv      t0, t1            # t0 = t1 (shift values)
        mv      t1, t2            # t1 = t2 (shift values)
        addi    t3, t3, -1        # decrement counter
        j       fib               # repeat loop

finish: addi    a0, x0, 1         # system call code 1 = print integer
        addi    a1, t0, 0         # a1 = t0 (value to print)
        ecall                     # print integer
        addi    a0, x0, 10        # system call code 10 = exit
        ecall                     # terminate program

```

1. RISC-V Directives  
.data: Specifies the data segment where program variables are stored  
.word: Allocates space for a 32-bit word in memory and initializes it with a value  
.text: Specifies the text segment where program instructions are stored  

1. Pseudo-instruction translations  
la (load address): Typically expands to auipc + addi instructions: auipc rd, offset_hi; addi rd, rd, offset_lo
mv (move): Expands to addi rd, rs, 0 (add immediate with 0)  
j (jump): Expands to jal x0, offset (jump without saving return address)  

1. Program output  
The program outputs 34, which is the 9th Fibonacci number (0-indexed).  

The code initializes t0=0 and t1=1, then calculates 9 iterations of the Fibonacci sequence: 0, 1, 1, 2, 3, 5, 8, 13, 21, 34  

4. Memory address  
The variable n would be stored at a memory address loaded by the instruction la t3, n. When you run the code in Venus, you'll see this address in the t3 register after that instruction executes.  

5. Calculating 13th Fibonacci number  
To calculate the 13th Fibonacci number (0-indexed), you would step through the code until after the instruction lw t3, 0(t3) executes, then manually change the value in t3 from 9 to 13, and continue execution. The result should be 233.
