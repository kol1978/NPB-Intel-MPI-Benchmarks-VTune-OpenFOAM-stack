#include "iso3dfd.h"
#include "iso3dfd_solver.hpp"
#include <omp.h>
#include <algorithm>
#include <cstdlib>

// ── Константы V2 ──────────────────────────────────────────────────────────
#define V2_SIMD_WIDTH        4       // 4 FP32 в __m128 (SSE4.2)
#define V2_L2_CACHE_BYTES    (256 * 1024)
#define V2_L2_RESERVE_FACTOR  0.8    // 80% от L2 = 204.8 КБ

// -----------------------------------------------------------------------
// Вычисление одной точки стенсиля 16-го порядка
// __attribute__((always_inline)) — форсит инлайн для векторизации
// -----------------------------------------------------------------------
static inline __attribute__((always_inline))
float stencil_point(const float* __restrict__ prev,
                     int x, int y, int z,
                     int n1, int n2,
                     const float* __restrict__ coeff) {
    float value = coeff[0] * prev[(z * n2 + y) * n1 + x];
    #pragma unroll
    for (int d = 1; d <= kHalfLength; d++) {
        value += coeff[d] * (
            prev[(z * n2 + y) * n1 + (x + d)] +
            prev[(z * n2 + y) * n1 + (x - d)] +
            prev[(z * n2 + (y + d)) * n1 + x] +
            prev[(z * n2 + (y - d)) * n1 + x] +
            prev[((z + d) * n2 + y) * n1 + x] +
            prev[((z - d) * n2 + y) * n1 + x]
        );
    }
    return value;
}

// -----------------------------------------------------------------------
// Последовательная версия
// -----------------------------------------------------------------------
static void compute_serial(const float* __restrict__ prev,
                           float*       __restrict__ next,
                           const float* __restrict__ vel,
                           int n1, int n2, int n3,
                           const float* coeff) {
    const float dt2 = DEFAULT_DT * DEFAULT_DT;
    for (int z = kHalfLength; z < n3 - kHalfLength; z++) {
        for (int y = kHalfLength; y < n2 - kHalfLength; y++) {
            #pragma omp simd
            for (int x = kHalfLength; x < n1 - kHalfLength; x++) {
                int idx = (z * n2 + y) * n1 + x;
                float s = stencil_point(prev, x, y, z, n1, n2, coeff);
                next[idx] = 2.0f * prev[idx] - next[idx]
                          + dt2 * vel[idx] * vel[idx] * s;
            }
        }
    }
}

// -----------------------------------------------------------------------
// V2: OpenMP с X-blocking + L2 micro-block + Z-streaming
//
// Иерархия циклов:
//   #pragma omp for collapse(2)  ← распределение (bz, by) по потокам
//     for bz (шаг b3)             ← L3-блок по Z
//       for by (шаг b2)           ← L3-блок по Y
//         for bx (шаг b1)         ← X-blocking
//           for by2 (шаг b2_l2)   ← L2 micro-block по Y
//             for z               ← Z-streaming
//               for y
//                 #pragma omp simd
//                   for x         ← SIMD 4×1 (SSE4.2)
//
// 2D LC = (2*h+1) × b1 × b2_l2 × 4 ≤ L2 × 0.8
// При b1=64, b2_l2=48: 17 × 64 × 48 × 4 = 204 КБ < 256 КБ  ✓
// -----------------------------------------------------------------------
static void compute_omp_v2(const float* __restrict__ prev,
                            float*       __restrict__ next,
                            const float* __restrict__ vel,
                            int n1, int n2, int n3,
                            int b1, int b2, int b3,
                            const float* coeff) {
    const float dt2 = DEFAULT_DT * DEFAULT_DT;
    const int h = kHalfLength;

    // ── Авто-вычисление L2 micro-block (b2_l2) ──────────────────────────
    int b2_l2 = (int)((V2_L2_CACHE_BYTES * V2_L2_RESERVE_FACTOR) /
                       ((2 * h + 1) * b1 * sizeof(float)));
    b2_l2 = (b2_l2 / V2_SIMD_WIDTH) * V2_SIMD_WIDTH;
    if (b2_l2 < V2_SIMD_WIDTH) b2_l2 = V2_SIMD_WIDTH;
    if (b2_l2 > b2) b2_l2 = b2;

    // Переопределение через переменную окружения
    if (const char* env = std::getenv("ISO3DFD_B2_L2")) {
        b2_l2 = atoi(env);
        if (b2_l2 < V2_SIMD_WIDTH) b2_l2 = V2_SIMD_WIDTH;
    }

    // ── Основной вычислительный цикл ───────────────────────────────────
    #pragma omp parallel
    {
        #pragma omp for collapse(2) schedule(static)
        for (int bz = h; bz < n3 - h; bz += b3) {
            for (int by = h; by < n2 - h; by += b2) {
                int z_end = std::min(bz + b3, n3 - h);
                int y_end_block = std::min(by + b2, n2 - h);

                for (int bx = h; bx < n1 - h; bx += b1) {
                    int x_end = std::min(bx + b1, n1 - h);

                    for (int by2 = by; by2 < y_end_block; by2 += b2_l2) {
                        int y_end_l2 = std::min(by2 + b2_l2, y_end_block);

                        for (int z = bz; z < z_end; z++) {
                            for (int y = by2; y < y_end_l2; y++) {
                                #pragma omp simd
                                for (int x = bx; x < x_end; x++) {
                                    int idx = (z * n2 + y) * n1 + x;
                                    float s = stencil_point(prev, x, y, z,
                                                           n1, n2, coeff);
                                    next[idx] = 2.0f * prev[idx] - next[idx]
                                              + dt2 * vel[idx] * vel[idx] * s;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// -----------------------------------------------------------------------
// Точка входа
// -----------------------------------------------------------------------
void compute_iteration(const float* __restrict__ prev,
                       float*       __restrict__ next,
                       const float* __restrict__ vel,
                       int n1, int n2, int n3,
                       int b1, int b2, int b3,
                       const float* coeff,
                       bool use_omp) {
    if (use_omp) {
        compute_omp_v2(prev, next, vel, n1, n2, n3, b1, b2, b3, coeff);
    } else {
        compute_serial(prev, next, vel, n1, n2, n3, coeff);
    }
}
