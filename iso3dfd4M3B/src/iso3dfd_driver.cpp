#include "iso3dfd.h"
#include "iso3dfd_driver.hpp"
#include "iso3dfd_solver.hpp"
#include "iso3dfd_grid.hpp"
#include <mpi.h>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include <cstdio>

// -----------------------------------------------------------------------
// Halo exchange: обмен граничными слоями с соседями по Z
// -----------------------------------------------------------------------
static void exchange_halo(float* prev, int n1, int n2, int nz_total,
                          int rank, int nprocs) {
    int halo_size = n1 * n2 * kHalfLength;
    size_t bytes  = (size_t)halo_size * sizeof(float);

    int prev_rank = (rank == 0)          ? MPI_PROC_NULL : rank - 1;
    int next_rank = (rank == nprocs - 1) ? MPI_PROC_NULL : rank + 1;

    float* send_bot = prev + (size_t)kHalfLength * n1 * n2;
    float* send_top = prev + (size_t)(nz_total - 2 * kHalfLength) * n1 * n2;

    float* recv_bot = prev;
    float* recv_top = prev + (size_t)(nz_total - kHalfLength) * n1 * n2;

    MPI_Sendrecv(send_top, halo_size, MPI_FLOAT, next_rank, 0,
                 recv_bot, halo_size, MPI_FLOAT, prev_rank, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    MPI_Sendrecv(send_bot, halo_size, MPI_FLOAT, prev_rank, 1,
                 recv_top, halo_size, MPI_FLOAT, next_rank, 1,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

// -----------------------------------------------------------------------
// Главный цикл решателя
// -----------------------------------------------------------------------
void run_driver(float* prev, float* next, float* vel,
                int n1, int n2, int nz_total, int nz_local,
                int b1, int b2, int b3,
                int niter, int rank, int nprocs,
                const float* coeff, bool use_omp) {

    // ── Диагностика V2 — только из rank 0, один раз ────────────────────
    if (rank == 0 && use_omp) {
        const int h = kHalfLength;
        int b2_l2 = (int)((256 * 1024 * 0.8) /
                         ((2 * h + 1) * b1 * sizeof(float)));
        b2_l2 = (b2_l2 / 4) * 4;
        if (b2_l2 < 4) b2_l2 = 4;
        if (b2_l2 > b2) b2_l2 = b2;
        if (const char* env = std::getenv("ISO3DFD_B2_L2")) {
            b2_l2 = atoi(env);
            if (b2_l2 < 4) b2_l2 = 4;
        }
        size_t lc_bytes = (size_t)(2 * h + 1) * b1 * b2_l2 * sizeof(float);
        printf("[v2] b1=%d b2=%d b3=%d  b2_l2=%d  tb_steps=0\n",
               b1, b2, b3, b2_l2);
        printf("[v2] 2D LC = %zu KB (L2 budget: 256 KB, reserve: 80%%)\n",
               lc_bytes / 1024);
        printf("[v2] ranks=%d  threads/rank=%s  nz_local=%d  nz_global=%d\n",
               nprocs,
               std::getenv("OMP_NUM_THREADS") ? std::getenv("OMP_NUM_THREADS") : "auto",
               nz_local, nz_local * nprocs);
        fflush(stdout);
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < niter; t++) {
        if (nprocs > 1) {
            exchange_halo(prev, n1, n2, nz_total, rank, nprocs);
        }

        compute_iteration(prev, next, vel, n1, n2, nz_total,
                          b1, b2, b3, coeff, use_omp);

        float* tmp = prev;
        prev = next;
        next = tmp;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    float time_s = std::chrono::duration<float>(t1 - t0).count();

    // ── MPI_Reduce: берём max время по всем рангам ──────────────────────
    float max_time_s;
    MPI_Reduce(&time_s, &max_time_s, 1, MPI_FLOAT, MPI_MAX, 0, MPI_COMM_WORLD);

    // ── Вывод: только rank 0, с глобальными размерами ──────────────────
    if (rank == 0) {
        int nz_global = nz_local * nprocs;
        PrintStats(n1, n2, nz_global, b1, b2, b3, niter);
        PrintSummary(n1, n2, nz_global, niter, max_time_s);
    }
}
