#include "iso3dfd.h"
#include "iso3dfd_grid.hpp"
#include <cstdlib>
#include <cstdio>
#include <cmath>

// -----------------------------------------------------------------------
// Выделение выровненной памяти (64 байта — одна cache-line на Westmere)
// -----------------------------------------------------------------------
float* Allocate(int size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, (size_t)size * sizeof(float)) != 0) {
        fprintf(stderr, "Error: allocation failed for %d floats\n", size);
        exit(1);
    }
    return (float*)ptr;
}

void Free(float* ptr) {
    free(ptr);
}

// -----------------------------------------------------------------------
// Коэффициенты стенсиля 16-го порядка (центральная конечная разность
// для второй производной). Значения — стандартные для N=8.
// -----------------------------------------------------------------------
void InitializeCoefficients(float* coeff, float dxyz) {
    float dxyz2 = dxyz * dxyz;
    coeff[0] = (-205.0f /   72.0f) / dxyz2;
    coeff[1] = (   8.0f /    5.0f) / dxyz2;
    coeff[2] = (  -1.0f /    5.0f) / dxyz2;
    coeff[3] = (   8.0f /  315.0f) / dxyz2;
    coeff[4] = (  -1.0f /  560.0f) / dxyz2;
    coeff[5] = (   8.0f / 3465.0f) / dxyz2;
    coeff[6] = (  -1.0f /11088.0f) / dxyz2;
    coeff[7] = (   8.0f /315315.0f) / dxyz2;
    coeff[8] = (  -1.0f /415800.0f) / dxyz2;
}

// -----------------------------------------------------------------------
// Инициализация: нули + импульс в центре домена
//
// half_length: размер halo (kHalfLength без TB, kHalfLength*(N+1) с TB).
// Параметр не влияет на инициализацию — импульс ставится в геометрический
// центр nz_total, нули заполняют весь массив включая расширенный halo.
// -----------------------------------------------------------------------
void Initialize(float* prev, float* next, float* vel,
                int n1, int n2, int n3, int half_length) {
    (void)half_length;  // не используется — halo заполняется нулями
    int total = n1 * n2 * n3;
    for (int i = 0; i < total; i++) {
        prev[i] = 0.0f;
        next[i] = 0.0f;
        vel[i]  = 1.0f;  // постоянная скорость
    }
    // Импульс в центре локального домена
    int cx = n1 / 2;
    int cy = n2 / 2;
    int cz = n3 / 2;
    int idx = (cz * n2 + cy) * n1 + cx;
    prev[idx] = 1.0f;
}

// -----------------------------------------------------------------------
// Вывод параметров запуска
// -----------------------------------------------------------------------
void PrintStats(int n1, int n2, int n3,
                int b1, int b2, int b3, int niter) {
    printf("iso3dfd — 3D isotropic finite-difference solver\n");
    printf("Grid:        %d x %d x %d\n", n1, n2, n3);
    printf("Block:       %d x %d x %d\n", b1, b2, b3);
    printf("Iterations:  %d\n", niter);
    printf("Half-length: %d (16th-order stencil)\n", kHalfLength);
    printf("------------------------------------------------\n");
}

// -----------------------------------------------------------------------
// Вывод результатов: время, производительность
// FLOP count: 9 mults + 48 adds + 6 (update) = 63 FLOPs/point/iteration
//
// n3 = nz_global (без halo) — физический размер домена.
// Interior = глобальный домен минус kHalfLength с каждой стороны.
// При temporal blocking физический interior не меняется —
// меняется только размер halo, который не входит в расчёт FLOPS.
// -----------------------------------------------------------------------
void PrintSummary(int n1, int n2, int n3, int niter, float time_s) {
    long long interior = (long long)(n1 - 2*kHalfLength)
                       * (n2 - 2*kHalfLength)
                       * (n3 - 2*kHalfLength);
    long long total_flops = interior * (long long)niter * 63LL;
    float gflops = (float)total_flops / time_s / 1e9f;
    printf("------------------------------------------------\n");
    printf("Time:         %.3f s\n", time_s);
    printf("Performance:  %.2f GFLOPS\n", gflops);
    printf("Lattice:      %lld points/iteration\n", interior);
}
