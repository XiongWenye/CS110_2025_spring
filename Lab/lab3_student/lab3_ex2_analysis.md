The register representing the variable k: t0  
The registers acting as pointers to the source and dest arrays: t1 and t2  
The assembly code for the loop found in the C code:  
```assembly
loop:
    slli t3, t0, 2
    add t4, t1, t3
    lw t5, 0(t4)
    beq t5, x0, exit
    add t6, t2, t3
    sw t5, 0(t6)
    addi t0, t0, 1
    jal x0, loop
```
How the pointers are manipulated in the assembly code: t0 (which is k) is multiplied by 4 (using slli t3, t0, 2) to get the offset in bytes. This offset is added to the base addresses of source (t1) and dest (t2) to get the addresses of the k-th elements.
