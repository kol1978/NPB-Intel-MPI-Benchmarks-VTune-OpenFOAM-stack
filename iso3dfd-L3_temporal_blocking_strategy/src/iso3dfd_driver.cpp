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
// halo: число слоёв для обмена (kHalfLength без TB, kHalfLength*(N+1) с TB)
// -----------------------------------------------------------------------
static void exchange_halo(float* prev, int n1, int n2, int nz_total,
                          int rank, int nprocs, int halo) {
    int halo_size = n1 * n2 * halo;

    int prev_rank = (rank == 0)          ? MPI_PROC_NULL : rank - 1;
    int next_rank = (rank == nprocs - 1) ? MPI_PROC_NULL : rank + 1;

    // Отправляем первые/последние halo слоёв interior, принимаем в halo-области
    float* send_bot = prev + (size_t)halo * n1 * n2;
    float* send_top = prev + (size_t)(nz_total - 2 * halo) * n1 * n2;

    float* recv_bot = prev;
    float* recv_top = prev + (size_t)(nz_total - halo) * n1 * n2;

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
                const float* coeff, bool use_omp,
                int tb_steps, int halo) {  // ── TB: новые параметры

    // ── Диагностика V2 ──────────────────────────────────────────────────
    if (rank == 0) {
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
        printf("[v2-tb] b1=%d b2=%d b3=%d  b2_l2=%d  tb_steps=%d  halo=%d\n",
               b1, b2, b3, b2_l2, tb_steps, halo);
        printf("[v2-tb] 2D LC = %zu KB (L2 budget: 256 KB, reserve: 80%%)\n",
               lc_bytes / 1024);
        if (tb_steps > 0) {
            // Рабочий набор temporal blocking: (2*h + tb_steps) z-плоскостей
            size_t tb_set = (size_t)(2 * h + tb_steps) * b1 * b2 * sizeof(float);
            printf("[v2-tb] TB working set = %zu KB (L3 budget/stream: ~1.75 MB)\n",
                   tb_set / 1024);
            int n_waves = niter / tb_steps;
            printf("[v2-tb] waves=%d (niter=%d / tb_steps=%d), rem=%d\n",
                   n_waves, niter, tb_steps, niter % tb_steps);
        }
        printf("[v2-tb] ranks=%d  threads/rank=%s  nz_local=%d  nz_global=%d\n",
               nprocs,
               std::getenv("OMP_NUM_THREADS") ? std::getenv("OMP_NUM_THREADS") : "auto",
               nz_local, nz_local * nprocs);
        fflush(stdout);
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    if (tb_steps > 0) {
        // ══ Temporal blocking mode ══════════════════════════════════════
        //
        // Цикл по «волнам»: каждая волна = tb_steps итераций.
        // Halo exchange — один раз на волну (расширенный halo).
        // Внутри волны — wave-front по Z в compute_temporal_block().
        //
        // После compute_temporal_block результат в next (как после
        // compute_iteration). Driver swap → результат в prev.
        //
        int n_waves = niter / tb_steps;
        int rem = niter % tb_steps;

        for (int w = 0; w < n_waves; w++) {
            // Обмен расширенным halo (один раз на волну)
            if (nprocs > 1) {
                exchange_halo(prev, n1, n2, nz_total, rank, nprocs, halo);
            }

            // N временных шагов через wave-front в L3
            compute_temporal_block(prev, next, vel,
                                   n1, n2, nz_total, nz_local,
                                   b1, b2, b3, halo, tb_steps,
                                   coeff, use_omp);

            // Swap: результат из next → prev (готов к следующей волне)
            float* tmp = prev;
            prev = next;
            next = tmp;
        }

        // Оставшиеся итерации (без temporal blocking)
        // Используем тот же halo — избыточно по объёму, но корректно
        for (int t = 0; t < rem; t++) {
            if (nprocs > 1) {
                exchange_halo(prev, n1, n2, nz_total, rank, nprocs, halo);
            }
            compute_iteration(prev, next, vel, n1, n2, nz_total,
                              b1, b2, b3, coeff, use_omp);
            float* tmp = prev;
            prev = next;
            next = tmp;
        }
    } else {
        // ══ Original mode (без temporal blocking) ══════════════════════
        for (int t = 0; t < niter; t++) {
            if (nprocs > 1) {
                exchange_halo(prev, n1, n2, nz_total, rank, nprocs, halo);
            }

            compute_iteration(prev, next, vel, n1, n2, nz_total,
                              b1, b2, b3, coeff, use_omp);

            float* tmp = prev;
            prev = next;
            next = tmp;
        }
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
