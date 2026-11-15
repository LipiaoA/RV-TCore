
#include <stdint.h>
#include <iostream>

typedef struct {
    // 行优先存储的二维数组
    uint64_t *data;
    uint64_t m;
    uint64_t n;
} Matrix;

#define N 4

volatile uint64_t A[N][N] = {
    { 1,  2,  3,  4},
    { 5,  6,  7,  8},
    { 9, 10, 11, 12},
    {13, 14, 15, 16}
};

volatile uint64_t B[N][N] = {
    { 1,  2,  3,  4},
    { 5,  6,  7,  8},
    { 9, 10, 11, 12},
    {13, 14, 15, 16}
};

volatile uint64_t C[N][N];

/* Expected multiplication result */
static const uint64_t EXPECTED[N][N] = {
    { 90, 100, 110, 120},
    {202, 228, 254, 280},
    {314, 356, 398, 440},
    {426, 484, 542, 600}
};

/* Benchmark status */
volatile uint64_t benchmark_status;
volatile uint64_t benchmark_error_count;
volatile uint64_t benchmark_error_mask;

static void matrix_multiply(Matrix *A, Matrix *B, Matrix *C)
{
    uint64_t m = A->m;
    uint64_t k = A->n;
    uint64_t n = B->n;
    uint64_t *a = A->data;
    uint64_t *b = B->data;
    uint64_t *c = C->data;

    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            uint64_t sum = 0;

            for (int l = 0; l < k; l++) {
                sum += a[i * k + l] * b[l * n + j];
            }

            c[i * n + j] = sum;
        }
    }
}

static bool verify_result(const Matrix *RESULT, const Matrix *EXPECTED)
{
    bool ret = true;
    uint64_t m = RESULT->m;
    uint64_t n = RESULT->n;
    ret &= RESULT->m == EXPECTED->m;
    ret &= RESULT->n == EXPECTED->n;
    for (int i = 0; i < m * n; i++) {
        ret &= RESULT->data[i] == EXPECTED->data[i];
    }
    return ret;
}

void print_matrix(const Matrix *M, const std::string &name)
{
    uint64_t m = M->m;
    uint64_t n = M->n;
    std::cout << name << std::endl;
    for (int i = 0; i < m; i++) {
        std::cout << "  ";
        for (int j = 0; j < n; j++) {
            std::cout << M->data[i * n + j] << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

bool benchmark_main(void)
{
    Matrix MA = { .data = (uint64_t *)A, .m = N, .n = N };
    Matrix MB = { .data = (uint64_t *)B, .m = N, .n = N };
    Matrix MC = { .data = (uint64_t *)C, .m = N, .n = N };
    Matrix ME = { .data = (uint64_t *)EXPECTED, .m = N, .n = N };
    matrix_multiply(&MA, &MB, &MC);
    // print_matrix(&MC, "Matrix Result");
    // print_matrix(&ME, "Matrix Expected");
    return verify_result(&MC, &ME);
}



int main(void)
{
    bool ret = benchmark_main();
    return ret ? 0 : 1;
}
