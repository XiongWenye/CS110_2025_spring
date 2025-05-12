#include <emmintrin.h> /* header file for the SSE intrinsics */
#include <time.h>
#include <stdio.h>
#include <math.h>

float **a;
float **b;
float **c;
float **c2;

int n = 10000;
int p = 8000;

void init(void) {
    a = malloc(n * sizeof(float *));
    b = malloc(4 * sizeof(float *));
    c = malloc(n * sizeof(float *));
    c2 = malloc(n * sizeof(float *));
    for (int i = 0; i < n; ++i) {
        a[i] = malloc(4 * sizeof(float));
        c[i] = malloc(p * sizeof(float));
        c2[i] = malloc(p * sizeof(float));
    }
    for (int i = 0; i < 4; ++i) {
        b[i] = malloc(p * sizeof(float));
    }

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < 4; ++j) {
            a[i][j] = (float) rand() / (float) RAND_MAX;
        }
    }

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < p; ++j) {
            b[i][j] = (float) rand() / (float) RAND_MAX;
        }
    }
}

void check_correctness(char *msg) {
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < p; ++j) {
            if (fabs(c[i][j] - c2[i][j]) > 1/1e6) {
                printf("%s incorrect!\n", msg);
                return;
            }
        }
    }
}

void naive_matmul(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // c = a * b
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < p; ++j) {
            c[i][j] = 0;
            for (int k = 0; k < 4; ++k) {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("naive: %f\n", (float) (end.tv_sec - start.tv_sec) + (float) (end.tv_nsec - start.tv_nsec) / 1000000000.0f);
}

void loop_unroll_matmul(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // c2 = a * b
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < p; j += 4) {
            // Handle 4 columns at a time
            if (j + 3 < p) {
                // Unrolled loop for 4 columns
                float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f, sum3 = 0.0f;
                
                // Multiply and accumulate for all 4 rows of a
                for (int k = 0; k < 4; ++k) {
                    sum0 += a[i][k] * b[k][j];
                    sum1 += a[i][k] * b[k][j+1];
                    sum2 += a[i][k] * b[k][j+2];
                    sum3 += a[i][k] * b[k][j+3];
                }
                
                c2[i][j] = sum0;
                c2[i][j+1] = sum1;
                c2[i][j+2] = sum2;
                c2[i][j+3] = sum3;
            } else {
                // Handle remaining columns individually
                for (int jj = j; jj < p; ++jj) {
                    c2[i][jj] = 0;
                    for (int k = 0; k < 4; ++k) {
                        c2[i][jj] += a[i][k] * b[k][jj];
                    }
                }
                break;
            }
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("unroll: %f\n", (float) (end.tv_sec - start.tv_sec) + (float) (end.tv_nsec - start.tv_nsec) / 1000000000.0f);
    check_correctness("loop_unroll_matmul");
}

void simd_matmul(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // c2 = a * b
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < p; ++j) {
            // Initialize accumulator to zero
            __m128 sum = _mm_setzero_ps();
            
            // Load all 4 elements from row i of a
            __m128 a_row = _mm_loadu_ps(a[i]);
            
            // Load all 4 elements from column j of b (one from each row of b)
            float temp[4];
            for (int k = 0; k < 4; ++k) {
                temp[k] = b[k][j];
            }
            __m128 b_col = _mm_loadu_ps(temp);
            
            // Multiply and accumulate
            sum = _mm_mul_ps(a_row, b_col);
            
            // Horizontal sum of 4 floats in the vector
            // Extract the sum of products into a single value
            float result_array[4];
            _mm_storeu_ps(result_array, sum);
            
            c2[i][j] = result_array[0] + result_array[1] + result_array[2] + result_array[3];
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("simd: %f\n", (float) (end.tv_sec - start.tv_sec) + (float) (end.tv_nsec - start.tv_nsec) / 1000000000.0f);
    check_correctness("simd_matmul");
}

void loop_unroll_simd_matmul(void) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // c2 = a * b
    for (int i = 0; i < n; ++i) {
        // Load the entire row of a once
        __m128 a_row = _mm_loadu_ps(a[i]);
        
        // Process 4 columns at a time
        for (int j = 0; j < p; j += 4) {
            if (j + 3 < p) {
                // Process 4 columns in parallel
                float temp0[4], temp1[4], temp2[4], temp3[4];
                for (int k = 0; k < 4; ++k) {
                    temp0[k] = b[k][j];
                    temp1[k] = b[k][j+1];
                    temp2[k] = b[k][j+2];
                    temp3[k] = b[k][j+3];
                }
                
                // Load columns from b
                __m128 b_col0 = _mm_loadu_ps(temp0);
                __m128 b_col1 = _mm_loadu_ps(temp1);
                __m128 b_col2 = _mm_loadu_ps(temp2);
                __m128 b_col3 = _mm_loadu_ps(temp3);
                
                // Multiply and accumulate
                __m128 prod0 = _mm_mul_ps(a_row, b_col0);
                __m128 prod1 = _mm_mul_ps(a_row, b_col1);
                __m128 prod2 = _mm_mul_ps(a_row, b_col2);
                __m128 prod3 = _mm_mul_ps(a_row, b_col3);
                
                // Store results for horizontal sum
                float result0[4], result1[4], result2[4], result3[4];
                _mm_storeu_ps(result0, prod0);
                _mm_storeu_ps(result1, prod1);
                _mm_storeu_ps(result2, prod2);
                _mm_storeu_ps(result3, prod3);
                
                // Calculate final sums
                c2[i][j]   = result0[0] + result0[1] + result0[2] + result0[3];
                c2[i][j+1] = result1[0] + result1[1] + result1[2] + result1[3];
                c2[i][j+2] = result2[0] + result2[1] + result2[2] + result2[3];
                c2[i][j+3] = result3[0] + result3[1] + result3[2] + result3[3];
            } else {
                // Handle remaining columns individually
                for (int jj = j; jj < p; ++jj) {
                    float temp[4];
                    for (int k = 0; k < 4; ++k) {
                        temp[k] = b[k][jj];
                    }
                    __m128 b_col = _mm_loadu_ps(temp);
                    __m128 prod = _mm_mul_ps(a_row, b_col);
                    
                    float result[4];
                    _mm_storeu_ps(result, prod);
                    c2[i][jj] = result[0] + result[1] + result[2] + result[3];
                }
                break;
            }
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("unroll+simd: %f\n", (float) (end.tv_sec - start.tv_sec) + (float) (end.tv_nsec - start.tv_nsec) / 1000000000.0f);
    check_correctness("loop_unroll_simd_matmul");
}

void deallocate(){
    for (int i = 0; i < n; ++i) {
        free(a[i]);
        free(c[i]);
        free(c2[i]);

    }
    for (int i = 0; i < 4; ++i) {
        free(b[i]);
    }
    free(a);
    free(b);
    free(c);
    free(c2);
    a = NULL;
    b = NULL;
    c = NULL;
    c2 = NULL;
}

int main(void) {
    init();

    naive_matmul();
    simd_matmul();
    loop_unroll_matmul();
    loop_unroll_simd_matmul();

    deallocate();
    return 0;
}

