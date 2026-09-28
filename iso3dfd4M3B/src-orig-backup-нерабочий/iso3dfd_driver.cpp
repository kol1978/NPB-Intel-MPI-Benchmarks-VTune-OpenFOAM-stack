#include "iso3dfd.h"
#include "iso3dfd_driver.hpp"
#include "iso3dfd_solver.hpp"
#include "iso3dfd_grid.hpp"
#include <mpi.h>
#include <cstring>
#include <chrono>

// -----------------------------------------------------------------------
// Halo exchange: обмен граничными слоями с соседями по Z
//
// Схема (kHalfLength = 8 слоёв в каждую сторону):
//
//   Ранг 0          Ранг 1          Ранг 2
//   |-----|         |-----|         |-----|
//   |halo | <--->   |halal |       |     |
//   |core |         |core | <--->   |core |
//   |     |         |     |         |halo |
//   |-----|         |-----|         |-----|
//
//   send_top -> recv_bot (каждый отправляет вверх, принимает снизу)
//   send_bot -> recv_top (каждый отправляет вниз, принимает сверху)
// -----------------------------------------------------------------------
static void exchange_halo(float* prev, int n1, int n2, int nz_total,
                          int rank, int nprocs) {
    int halo_size = n1 * n2 * kHalfLength;  // число float в одном halo
    size_t bytes  = (size_t)halo_size * sizeof(float);

    // Соседи
    int prev_rank = (rank == 0)          ? MPI_PROC_NULL : rank - 1;
    int next_rank = (rank == nprocs - 1) ? MPI_PROC_NULL : rank + 1;

    // Граничные слои в prev:
    //   Нижний (для отправки вниз):  [kHalfLength .. 2*kHalfLength)
    //   Верхний (для отправки вверх): [nz_total - 2*kHalfLength .. nz_total - kHalfLength)
    float* send_bot = prev + (size_t)kHalfLength * n1 * n2;
    float* send_top = prev + (size_t)(nz_total - 2 * kHalfLength) * n1 * n2;

    // Куда принимаем:
    //   Снизу (от prev_rank): [0 .. kHalfLength)          — заполняем нижний halo
    //   Сверху (от next_rank): [nz_total - kHalfLength .. nz_total) — заполняем верхний halo
    float* recv_bot = prev;
    float* recv_top = prev + (size_t)(nz_total - kHalfLength) * n1 * n2;

    // Обмен: отправляем верх -> принимаем снизу, отправляем низ -> принимаем сверху
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

    auto t0 = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < niter; t++) {
        // 1. Обмен halo (заполняем граничные слои в prev)
        if (nprocs > 1) {
            exchange_halo(prev, n1, n2, nz_total, rank, nprocs);
        }

        // 2. Вычисление стенсиля (prev -> next)
        compute_iteration(prev, next, vel, n1, n2, nz_total,
                          b1, b2, b3, coeff, use_omp);

        // 3. Смена буферов
        float* tmp = prev;
        prev = next;
        next = tmp;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    float time_s = std::chrono::duration<float>(t1 - t0).count();

    // Вывод: только ранг 0
    if (rank == 0) {
        PrintStats(n1, n2, nz_local, b1, b2, b3, niter);
        PrintSummary(n1, n2, nz_local, niter, time_s);
    }
}
