#include "iso3dfd.h"
#include "iso3dfd_grid.hpp"
#include "iso3dfd_driver.hpp"
#include <mpi.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

// -----------------------------------------------------------------------
// Использование
// -----------------------------------------------------------------------
static void usage(const char* prog) {
    printf("Usage: %s n1 n2 n3 b1 b2 b3 niter [mode]\n", prog);
    printf("  n1 n2 n3   — размеры сетки (x y z)\n");
    printf("  b1 b2 b3   — размеры блока для cache-blocking (spatial tiling)\n");
    printf("  niter      — число итераций\n");
    printf("  mode       — sequential | pure_mpi | hybrid | pure_omp\n");
    printf("               (по умолчанию: sequential)\n");
    printf("\n");
    printf("  ISO3DFD_TB_STEPS=N  — temporal blocking: N шагов в wave-front (0=off)\n");
    printf("                        Constraint: b3 >= 8*N + kHalfLength*2 + 1\n");
    printf("\n");
    printf("  ISO3DFD_B2_L2=K     — nested spatial tiling: размер L2-микроблока по Y\n");
    printf("                        Formula: floor(256KB * 0.8 / (17 * b1 * 4))\n");
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

    // ── TB: чтение ISO3DFD_TB_STEPS из окружения ──────────────────────────
    int tb_steps = 0;
    const char* tb_env = getenv("ISO3DFD_TB_STEPS");
    if (tb_env) {
        tb_steps = atoi(tb_env);
        if (tb_steps < 0) tb_steps = 0;
    }

    // ── TB: проверка ограничения b3 >= 8*N + 2*kHalfLength + 1 ───────────
    // Волновой фронт: каждый T-шаг требует 8 слоёв по Z (kHalfLength).
    // Рабочий блок: b3. Общий минимум: 8*N (wave-front) + b3 (рабочая область).
    // Упрощённая формула: b3 >= 8*tb_steps + kHalfLength*2 + 1
    if (tb_steps > 0) {
        int b3_min = 8 * tb_steps + 2 * kHalfLength + 1;
        if (b3 < b3_min) {
            if (rank == 0) {
                fprintf(stderr,
                    "Error: temporal blocking requires b3 >= %d (8*%d + 2*%d + 1), got b3=%d\n",
                    b3_min, tb_steps, kHalfLength, b3);
                fprintf(stderr, "Fix: increase b3 or decrease ISO3DFD_TB_STEPS\n");
            }
            MPI_Finalize();
            return 1;
        }
        // niter должно делиться на tb_steps — каждая «волна» = tb_steps итераций
        if (niter % tb_steps != 0) {
            if (rank == 0) {
                fprintf(stderr,
                    "Warning: niter (%d) not divisible by tb_steps (%d), "
                    "last wave will be partial\n", niter, tb_steps);
            }
        }
    }

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

    // ── TB: размер halo зависит от tb_steps ──────────────────────────────
    // Без TB: halo = kHalfLength (8) с каждой стороны
    // С TB (N шагов): halo = kHalfLength * (tb_steps + 1) с каждой стороны,
    //   т.к. wave-front требует 8*N дополнительных слоёв для разворота
    int halo = kHalfLength;
    if (tb_steps > 0) {
        halo = kHalfLength * (tb_steps + 1);
    }
    int nz_total = nz_local + 2 * halo;

    // Коэффициенты стенсиля
    float coeff[kHalfLength + 1];
    InitializeCoefficients(coeff, DEFAULT_DXYZ);

    // Аллокация локальных массивов (с расширенным halo)
    int local_size = n1 * n2 * nz_total;
    float* prev = Allocate(local_size);
    float* next = Allocate(local_size);
    float* vel  = Allocate(local_size);

    // Инициализация
    Initialize(prev, next, vel, n1, n2, nz_total, halo);

    // ── Отладочный вывод (rank 0) ────────────────────────────────────────
    if (rank == 0) {
        printf("[v2-tb] b1=%d b2=%d b3=%d  tb_steps=%d  halo=%d\n",
               b1, b2, b3, tb_steps, halo);
        printf("[v2-tb] ranks=%d  threads/rank=%d  nz_local=%d  nz_global=%d\n",
               nprocs, use_omp ? atoi(getenv("OMP_NUM_THREADS") ?: "1") : 1,
               nz_local, n3);
    }

    // Запуск решателя
    run_driver(prev, next, vel,
               n1, n2, nz_total, nz_local,
               b1, b2, b3, niter,
               rank, nprocs, coeff, use_omp,
               tb_steps, halo);  // ── TB: новые параметры

    // Очистка
    Free(prev);
    Free(next);
    Free(vel);

    MPI_Finalize();
    return 0;
}
