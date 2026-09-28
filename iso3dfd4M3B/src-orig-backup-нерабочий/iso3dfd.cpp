#include "iso3dfd.h"
#include "iso3dfd_grid.hpp"
#include "iso3dfd_driver.hpp"
#include <mpi.h>
#include <cstdio>
#include <cstring>
#include <string>

// -----------------------------------------------------------------------
// Использование
// -----------------------------------------------------------------------
static void usage(const char* prog) {
    printf("Usage: %s n1 n2 n3 b1 b2 b3 niter [mode]\n", prog);
    printf("  n1 n2 n3   — размеры сетки (x y z)\n");
    printf("  b1 b2 b3   — размеры блока для cache-blocking\n");
    printf("  niter      — число итераций\n");
    printf("  mode       — sequential | pure_mpi | hybrid | pure_omp\n");
    printf("               (по умолчанию: sequential)\n");
    exit(1);
}

// -----------------------------------------------------------------------
// MAIN
// -----------------------------------------------------------------------
int main(int argc, char** argv) {
    // MPI init
    MPI_Init(&argc, &argv);
    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    // Парсинг аргументов
    if (argc < 8) {
        if (rank == 0) usage(argv[0]);
        MPI_Finalize();
        return 1;
    }

    int n1 = atoi(argv[1]);  // x
    int n2 = atoi(argv[2]);  // y
    int n3 = atoi(argv[3]);  // z (декомпозируется по рангам)
    int b1 = atoi(argv[4]);
    int b2 = atoi(argv[5]);
    int b3 = atoi(argv[6]);
    int niter = atoi(argv[7]);

    std::string mode = (argc >= 9) ? argv[8] : "sequential";

    // Определение режима
    bool use_omp;
    if (mode == "sequential") {
        use_omp = false;
        nprocs = 1;  // логически 1 ранг
    } else if (mode == "pure_mpi") {
        use_omp = false;
    } else if (mode == "hybrid") {
        use_omp = true;
    } else if (mode == "pure_omp") {
        use_omp = true;
        nprocs = 1;
    } else {
        if (rank == 0) {
            fprintf(stderr, "Unknown mode: %s\n", mode.c_str());
            fprintf(stderr, "Use: sequential | pure_mpi | hybrid | pure_omp\n");
        }
        MPI_Finalize();
        return 1;
    }

    // Проверка делимости Z
    if (n3 % nprocs != 0) {
        if (rank == 0) {
            fprintf(stderr, "Error: n3 (%d) must be divisible by nprocs (%d)\n", n3, nprocs);
        }
        MPI_Finalize();
        return 1;
    }

    // Декомпозиция по Z
    int nz_local = n3 / nprocs;
    int nz_total = nz_local + 2 * kHalfLength;  // + halo с обеих сторон

    // Коэффициенты стенсиля
    float coeff[kHalfLength + 1];
    InitializeCoefficients(coeff, DEFAULT_DXYZ);

    // Аллокация локальных массивов (с halo)
    int local_size = n1 * n2 * nz_total;
    float* prev = Allocate(local_size);
    float* next = Allocate(local_size);
    float* vel  = Allocate(local_size);

    // Инициализация
    Initialize(prev, next, vel, n1, n2, nz_total, kHalfLength);

    // Запуск решателя
    run_driver(prev, next, vel,
               n1, n2, nz_total, nz_local,
               b1, b2, b3, niter,
               rank, nprocs, coeff, use_omp);

    // Очистка
    Free(prev);
    Free(next);
    Free(vel);

    MPI_Finalize();
    return 0;
}
