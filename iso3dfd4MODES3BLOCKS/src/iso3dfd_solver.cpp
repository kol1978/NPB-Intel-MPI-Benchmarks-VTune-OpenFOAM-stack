#include "iso3dfd.h"
#include "iso3dfd_solver.hpp"
#include <omp.h>
#include <algorithm>

// -----------------------------------------------------------------------
// Вычисление одной точки стенсиля 16-го порядка
// Индексация: ptr[(z * n2 + y) * n1 + x]
// -----------------------------------------------------------------------
static inline float stencil_point(const float* prev,
                                   int x, int y, int z,
                                   int n1, int n2,
                                   const float* coeff) {
    float value = coeff[0] * prev[(z * n2 + y) * n1 + x];
    #pragma unroll
    for (int d = 1; d <= kHalfLength; d++) {
        value += coeff[d] * (
            // X (самый быстрый — contiguous)
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

// -----------------------------------------------------------------------
// Последовательная версия: линейный обход, hardware prefetcher работает
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
// OpenMP-версия с cache-blocking
// Блок (b1,b2,b3) определяет L2-тайлинг. Итерация по блокам —
// L3-уровень (каждый блок обрабатывается одним потоком, данные
// остаются в L2/L1). collapse(2) распараллеливает по z,y внутри блока.
// -----------------------------------------------------------------------
static void compute_omp(const float* __restrict__ prev,
                        float*       __restrict__ next,
                        const float* __restrict__ vel,
                        int n1, int n2, int n3,
                        int b1, int b2, int b3,
                        const float* coeff) {
    const float dt2 = DEFAULT_DT * DEFAULT_DT;

    #pragma omp parallel
    {
        // Итерация по блокам (L3-уровень)
        for (int bz = kHalfLength; bz < n3 - kHalfLength; bz += b3) {
            int z_end = std::min(bz + b3, n3 - kHalfLength);
            for (int by = kHalfLength; by < n2 - kHalfLength; by += b2) {
                int y_end = std::min(by + b2, n2 - kHalfLength);
                #pragma omp for collapse(2) schedule(static)
                for (int z = bz; z < z_end; z++) {
                    for (int y = by; y < y_end; y++) {
                        #pragma omp simd
                        for (int x = kHalfLength; x < n1 - kHalfLength; x++) {
                            int idx = (z * n2 + y) * n1 + x;
                            float s = stencil_point(prev, x, y, z, n1, n2, coeff);
                            next[idx] = 2.0f * prev[idx] - next[idx]
                                      + dt2 * vel[idx] * vel[idx] * s;
                        }
                    }
                } // implicit barrier
            }
        }
    }
}

// -----------------------------------------------------------------------
// Точка входа: выбор версии
// -----------------------------------------------------------------------
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
