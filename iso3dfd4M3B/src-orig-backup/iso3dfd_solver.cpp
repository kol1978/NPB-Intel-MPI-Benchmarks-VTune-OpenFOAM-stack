#include "iso3dfd.h"
#include "iso3dfd_solver.hpp"
#include <omp.h>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <emmintrin.h>  // SSE2: _mm_loadu_ps, _mm_add_ps, _mm_mul_ps, ...

// =======================================================================
// BASELINE: stencil_point — для compute_serial (без оптимизаций)
// =======================================================================
static inline float stencil_point(const float* prev,
                                   int x, int y, int z,
                                   int n1, int n2,
                                   const float* coeff) {
    float value = coeff[0] * prev[(z * n2 + y) * n1 + x];
    #pragma unroll
    for (int d = 1; d <= kHalfLength; d++) {
        value += coeff[d] * (
            // X (contiguous)
            prev[(z * n2 + y) * n1 + (x + d)] +
            prev[(z * n2 + y) * n1 + (x - d)] +
            // Y
            prev[(z * n2 + (y + d)) * n1 + x] +
            prev[(z * n2 + (y - d)) * n1 + x] +
            // Z
            prev[((z + d) * n2 + y) * n1 + x] +
            prev[((z - d) * n2 + y) * n1 + x]
        );
    }
    return value;
}

// =======================================================================
// BASELINE: последовательная версия (без изменений)
// =======================================================================
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

// =======================================================================
// V2: Z-streaming + L2 micro-block + SSE4.2 (4x1 vector folding)
// =======================================================================

// Чтение env-переменной
static int get_env_int(const char* name, int default_val) {
    const char* val = getenv(name);
    return val ? atoi(val) : default_val;
}

static void compute_omp(const float* __restrict__ prev,
                        float*       __restrict__ next,
                        const float* __restrict__ vel,
                        int n1, int n2, int n3,
                        int b1, int b2, int b3,
                        const float* coeff) {
    const float dt2 = DEFAULT_DT * DEFAULT_DT;
    const int h = kHalfLength;      // 8
    const int sw = SIMD_WIDTH;      // 4

    // --- L2 sub-block size (b2_l2) ---
    // 2D LC = (2*h+1) * b1 * b2_l2 * sizeof(float) <= L2_CACHE * reserve
    int b2_l2 = get_env_int("ISO3DFD_B2_L2", 0);
    if (b2_l2 <= 0) {
        b2_l2 = (int)(L2_CACHE_BYTES * L2_RESERVE_FACTOR /
                      ((2 * h + 1) * b1 * sizeof(float)));
        b2_l2 = std::min(b2_l2, b2);
        b2_l2 = std::max(b2_l2, 1);
    }

    // --- Temporal blocking (заготовка для этапа 2) ---
    int tb_steps = get_env_int("ISO3DFD_TB_STEPS", TB_STEPS_DEFAULT);
    // Пока не реализовано — оставлено как placeholder

    // Вывод параметров (один раз, из потока 0)
    #pragma omp single
    {
        printf("[v2] b1=%d b2=%d b3=%d  b2_l2=%d  tb_steps=%d\n",
               b1, b2, b3, b2_l2, tb_steps);
        int dlc = (2 * h + 1) * b1 * b2_l2 * sizeof(float);
        printf("[v2] 2D LC = %d KB (L2 budget: %d KB, reserve: %.0f%%)\n",
               dlc / 1024, L2_CACHE_BYTES / 1024,
               (1.0f - (float)dlc / L2_CACHE_BYTES) * 100.0f);
    }

    // --- Broadcast coefficients to SSE vectors ---
    __m128 vc[kHalfLength + 1];
    for (int d = 0; d <= h; d++)
        vc[d] = _mm_set1_ps(coeff[d]);
    __m128 vdt2 = _mm_set1_ps(dt2);
    __m128 v2   = _mm_set1_ps(2.0f);

    #pragma omp parallel
    {
        // === L3 block iteration (bz, by) ===
        for (int bz = h; bz < n3 - h; bz += b3) {
            int z_end = std::min(bz + b3, n3 - h);

            for (int by = h; by < n2 - h; by += b2) {
                int y_end_block = std::min(by + b2, n2 - h);

                // Count X blocks for combined parallelization
                int num_bx = (n1 - 2 * h + b1 - 1) / b1;

                // === L2 sub-block iteration (by2) ===
                for (int by2 = by; by2 < y_end_block; by2 += b2_l2) {
                    int y_end_l2 = std::min(by2 + b2_l2, y_end_block);
                    int num_y_l2 = y_end_l2 - by2;

                    // Combined work units: (y, bx) pairs
                    int total_units = num_y_l2 * num_bx;

                    // === Parallelize over (y, bx) pairs ===
                    #pragma omp for schedule(static)
                    for (int unit = 0; unit < total_units; unit++) {
                        int bx_idx = unit / num_y_l2;
                        int y = by2 + (unit % num_y_l2);
                        int bx = h + bx_idx * b1;
                        int x_end_block = std::min(bx + b1, n1 - h);

                        // === X loop: chunks of 4 (SSE4.2 4x1 folding) ===
                        for (int x = bx; x < x_end_block; x += sw) {
                            int x_chunk = std::min(sw, x_end_block - x);

                            if (x_chunk < sw) {
                                // --- Scalar tail (rare: n1-2h not divisible by 4) ---
                                for (int xi = x; xi < x_end_block; xi++) {
                                    float front_s[9], back_s[8];
                                    for (int d = 0; d <= h; d++)
                                        front_s[d] = prev[((bz + d) * n2 + y) * n1 + xi];
                                    for (int d = 1; d <= h; d++)
                                        back_s[d-1] = prev[((bz - d) * n2 + y) * n1 + xi];

                                    for (int z = bz; z < z_end; z++) {
                                        int idx = (z * n2 + y) * n1 + xi;
                                        float value = coeff[0] * front_s[0];
                                        for (int d = 1; d <= h; d++) {
                                            value += coeff[d] * (
                                                front_s[d] + back_s[d-1] +
                                                prev[idx + d] + prev[idx - d] +
                                                prev[(z * n2 + (y+d)) * n1 + xi] +
                                                prev[(z * n2 + (y-d)) * n1 + xi]
                                            );
                                        }
                                        next[idx] = 2.0f * front_s[0] - next[idx]
                                                  + dt2 * vel[idx] * vel[idx] * value;
                                        for (int d = h-1; d > 0; d--) back_s[d] = back_s[d-1];
                                        back_s[0] = front_s[0];
                                        for (int d = 0; d < h; d++) front_s[d] = front_s[d+1];
                                        if (z + 1 < z_end)
                                            front_s[h] = prev[((z + h + 1) * n2 + y) * n1 + xi];
                                    }
                                }
                                continue;
                            }

                            // --- Z-streaming with SSE4.2 intrinsics ---
                            // Initialize front[0..8] and back[0..7] as __m128
                            __m128 front[9], back[8];
                            for (int d = 0; d <= h; d++)
                                front[d] = _mm_loadu_ps(
                                    &prev[((bz + d) * n2 + y) * n1 + x]);
                            for (int d = 1; d <= h; d++)
                                back[d-1] = _mm_loadu_ps(
                                    &prev[((bz - d) * n2 + y) * n1 + x]);

                            // Stream through Z
                            for (int z = bz; z < z_end; z++) {
                                int base = (z * n2 + y) * n1 + x;

                                // Compute stencil: 49-point, 16th order
                                __m128 value = _mm_mul_ps(vc[0], front[0]);

                                #pragma unroll
                                for (int d = 1; d <= h; d++) {
                                    // Z neighbors: from register buffers (FREE)
                                    __m128 z_n = _mm_add_ps(front[d], back[d-1]);

                                    // X neighbors: shifted loads (contiguous, prefetched)
                                    __m128 x_p = _mm_loadu_ps(&prev[base + d]);
                                    __m128 x_n = _mm_loadu_ps(&prev[base - d]);
                                    __m128 x_n2 = _mm_add_ps(x_p, x_n);

                                    // Y neighbors: strided loads
                                    __m128 y_p = _mm_loadu_ps(
                                        &prev[(z * n2 + (y + d)) * n1 + x]);
                                    __m128 y_n = _mm_loadu_ps(
                                        &prev[(z * n2 + (y - d)) * n1 + x]);
                                    __m128 y_n2 = _mm_add_ps(y_p, y_n);

                                    // Accumulate: value += coeff[d] * (z_n + x_n2 + y_n2)
                                    __m128 all = _mm_add_ps(
                                        _mm_add_ps(z_n, x_n2), y_n2);
                                    value = _mm_add_ps(value,
                                        _mm_mul_ps(vc[d], all));
                                }

                                // Write: next = 2*prev - next + dt2*vel^2*value
                                __m128 vvel  = _mm_loadu_ps(&vel[base]);
                                __m128 vvel2 = _mm_mul_ps(vvel, vvel);
                                __m128 vnext = _mm_loadu_ps(&next[base]);
                                __m128 result = _mm_add_ps(
                                    _mm_sub_ps(
                                        _mm_mul_ps(v2, front[0]), vnext),
                                    _mm_mul_ps(
                                        _mm_mul_ps(vdt2, vvel2), value));
                                _mm_storeu_ps(&next[base], result);

                                // Shift front/back (register moves + 1 new load)
                                for (int d = h - 1; d > 0; d--)
                                    back[d] = back[d-1];
                                back[0] = front[0];
                                for (int d = 0; d < h; d++)
                                    front[d] = front[d+1];
                                if (z + 1 < z_end)
                                    front[h] = _mm_loadu_ps(
                                        &prev[((z + h + 1) * n2 + y) * n1 + x]);

                            } // end Z stream
                        } // end X chunks
                    } // end parallel for (y, bx) units
                } // end L2 sub-block (by2)
            } // end Y block (by)
        } // end Z block (bz)
    } // end parallel
}

// =======================================================================
// Точка входа: выбор версии
// =======================================================================
void compute_iteration(const float* __restrict__ prev,
                       float*       __restrict__ next,
                       const float* __restrict__ vel,
                       int n1, int n2, int n3,
                       int b1, int b2, int b3,
                       const float* coeff,
                       bool use_omp) {
    if (use_omp) {
        compute_omp(prev, next, vel, n1, n2, n3, b1, b2, b3, coeff);
    } else {
        compute_serial(prev, next, vel, n1, n2, n3, coeff);
    }
}
