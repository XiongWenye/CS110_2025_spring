#let sol = content => {
  box([*Solution: * #content], stroke: 1pt, inset: 10pt, width: 100%)
}

// Solution Example:
// #let t1 = (
//   [Yousr Ans],
//   [Yousr Ans],
//   [Yousr Ans],
//   [Yousr Ans],
// )
//
// #let t2-3 = sol[
//   Your Ans
// ]
//
// $ 1 + 1 = 2 $ for math equation

#let t1 = (
  [F],
  [F],
  [F],
  [F],
)

#let t2-1 = (
  [32 bytes],
  [256 bytes],
  [2],
  [8 blocks],
  [4],
  [Tag (5) | Index (2) | Offset (5)],
)

#let t2-2 = (
  [Miss],
  [Miss],
  [Miss],
  [Replacement],
  [Replacement],
  [Miss],
  [Replacement],
  [Hit],
  [Replacement],
  [Replacement],
  [Hit],
  [Hit],
  [Replacement],
  [Hit],
)

#let t2-3 = sol[
  Cache Hit Rate = (Number of Hits) / (Number of total Accesses) = $4 / 14 = 0.2857 = 28.57%$

  Improving Hit Rate without changing Capacity:

  Increasing associativity: Convert from 2-way to 4-way or fully associative (reduces conflict misses)

  Changing block size: Adjust block size (smaller for random access patterns, larger for sequential access)

]
#let t3-1 = sol[
  GMR = (L1 Miss Rate) * (L2 Miss Rate) * (L3 Miss Rate) = $0.08 * 0.35 * 0.10 = 0.0028 = 0.28%$
]
#let t3-2 = sol[
  AMAT = Hit time of L1 + Miss rate of L1 \* (Hit time of L2 + Miss rate of L2 \*(Hit time of L3 + Miss rate of L3 \* Memory access time))

  AMAT = 2 + 0.08 (12 + 0.35(44 + 0.10 \* 320)) = 2 + 0.08 (12 + 0.35 \* 76) = 2 + 0.08 \* 38.6 = 2 + 3.088 = 5.088 cycles
]
#let t3-3 = sol[
  New AMAT = 3.2 + 0.06 (12 + 0.35 (44 + 0.10 \* 320)) = 3.2 + 0.06 \* 38.6 = 3.2 + 2.316 = 5.516 cycles
]
#let t4-1 = sol[
  First we need to calculate the number of total accesses to the cache. 
  For each i from 0 to 63, inner loop goes from i+1 to 63, so the number of comparisons is:
  $Sigma_{i=0}^{63} (63 - i) = 63 + 62 + ... + 1 = 63 * (63 + 1) / 2 = 2016$
  For each comparison, we access two bodies, so total accesses = 2 \* 2016 = 4032.

  Now, we can calculate the number of cache hits, which is 2992.

  Cache hit rate = (Number of Hits) / (Number of total Accesses) = $2992 / 4032 approx 0.742 = 74.2%$


]
#let t4-2 = sol[
  We can modify the code from:
    ```c
    for (int i = 0; i < 64; i++) {
      for (int j = i + 1; j < 64; j++) {
          if (is_collide(bodies[i], bodies[j])) {
              count++;
          }
      }
    }
    ```
  to:
    ```c
    for (int i = 1; i < 64; i+=4) {
      for (int j = i; j < 64; j+=4) {
          for (int k = i; k < min(i + 4, 64); k++) {
              for (int l = max(j, k + 1); l < min(j + 4, 64); l++) {
                  if (is_collide(bodies[k], bodies[l])) {
                      count++;
                  }
              }
          }
      } 
    }
    ```
  Now the code processes bodies in blocks of 4+4 = 8, which will improve cache locality.
  This will reduce the number of cache misses and improve the cache hit rate. The inner loop will now proccess pairs of bodies within the same block and decreases the number of cache accesses.
]