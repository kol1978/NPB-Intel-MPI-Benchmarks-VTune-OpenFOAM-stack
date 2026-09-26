#include "../include/iso3dfd.h"

#ifdef USE_MPI
#include <mpi.h>
#endif

// Stencil coefficients (16th order)
static const float stencil_coeff[kHalfLength + 1] = {
    -2.547582679417302f,
     0.424116832858625f,
    -0.073032731286267f,
     0.014939560314436f,
    -0.003218089835269f,
     0.000686683936981f,
    -0.000144468614715f,
     0.000029882060415f,
    -0.000005989565436f
};

// ── Single iteration kernel (CPU) ──
void Iso3dfdIteration(float* ptr_next_base, float* ptr_prev_base,
                      float* ptr_vel_base, float* coeff,
                      size_t n1, size_t n2, size_t n3,
                      size_t n1_block, size_t n2_block, size_t n3_block) {
  size_t n3_end = n3 - kHalfLength;
  size_t n2_end = n2 - kHalfLength;
  size_t n1_end = n1 - kHalfLength;
  size_t dimn1n2 = n1 * n2;

  for (size_t bz = kHalfLength; bz < n3_end; bz += n3_block) {
    for (size_t by = kHalfLength; by < n2_end; by += n2_block) {
      for (size_t bx = kHalfLength; bx < n1_end; bx += n1_block) {
        size_t iz_end = std::min(bz + n3_block, n3_end);
        size_t iy_end = std::min(by + n2_block, n2_end);
        size_t ix_end = std::min(n1_block, n1_end - bx);

        #pragma omp parallel for collapse(2) schedule(dynamic)
        for (size_t iz = bz; iz < iz_end; iz++) {
          for (size_t iy = by; iy < iy_end; iy++) {
            size_t offset = iy * n1 + iz * dimn1n2;
            for (size_t ix = bx; ix < ix_end; ix++) {
              size_t gid = ix + offset;
              float value = ptr_prev_base[gid] * coeff[0];
              for (size_t n = 1; n <= kHalfLength; n++) {
                value += ptr_prev_base[gid + n] * coeff[n]
                       - ptr_prev_base[gid - n] * coeff[n];
                value += ptr_prev_base[gid + n * n1] * coeff[n]
                       - ptr_prev_base[gid - n * n1] * coeff[n];
                value += ptr_prev_base[gid + n * dimn1n2] * coeff[n]
                       - ptr_prev_base[gid - n * dimn1n2] * coeff[n];
              }
              ptr_next_base[gid] = 2.0f * ptr_prev_base[gid]
                                 - ptr_next_base[gid]
                                 + value * ptr_vel_base[gid];
            }
          }
        }
      }
    }
  }
}

#ifdef USE_MPI
// ── MPI halo exchange along Z ──
void ExchangeHaloZ(float* ptr, size_t n1, size_t n2, size_t n3_alloc,
                   int rank, int nprocs) {
  size_t layer_size = n1 * n2;
  size_t halo_size = kHalfLength * layer_size;

  MPI_Request requests[4];
  int req_count = 0;

  // Exchange with rank above (rank + 1)
  if (rank < nprocs - 1) {
    float* send_buf = &ptr[(n3_alloc - 2 * kHalfLength) * layer_size];
    float* recv_buf = &ptr[(n3_alloc - kHalfLength) * layer_size];
    MPI_Isend(send_buf, halo_size, MPI_FLOAT, rank + 1, 0,
              MPI_COMM_WORLD, &requests[req_count++]);
    MPI_Irecv(recv_buf, halo_size, MPI_FLOAT, rank + 1, 1,
              MPI_COMM_WORLD, &requests[req_count++]);
  }

  // Exchange with rank below (rank - 1)
  if (rank > 0) {
    float* send_buf = &ptr[kHalfLength * layer_size];
    float* recv_buf = &ptr[0];
    MPI_Isend(send_buf, halo_size, MPI_FLOAT, rank - 1, 1,
              MPI_COMM_WORLD, &requests[req_count++]);
    MPI_Irecv(recv_buf, halo_size, MPI_FLOAT, rank - 1, 0,
              MPI_COMM_WORLD, &requests[req_count++]);
  }

  if (req_count > 0) {
    MPI_Waitall(req_count, requests, MPI_STATUSES_IGNORE);
  }
}
#endif

// ── Time loop ──
void Iso3dfd(float* ptr_next_base, float* ptr_prev_base, float* ptr_vel_base,
             float* coeff,
             size_t n1, size_t n2, size_t n3,
             size_t n1_block, size_t n2_block, size_t n3_block,
             size_t num_iterations
#ifdef USE_MPI
             , int rank, int nprocs
#endif
             ) {
  for (size_t iter = 0; iter < num_iterations; iter++) {
#ifdef USE_MPI
    ExchangeHaloZ(ptr_prev_base, n1, n2, n3, rank, nprocs);
#endif
    Iso3dfdIteration(ptr_next_base, ptr_prev_base, ptr_vel_base, coeff,
                     n1, n2, n3, n1_block, n2_block, n3_block);
    std::swap(ptr_prev_base, ptr_next_base);
  }
}

// ── Main ──
int main(int argc, char* argv[]) {
#ifdef USE_MPI
  MPI_Init(&argc, &argv);
  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
#else
  int rank = 0;
  int nprocs = 1;
#endif

  // ── Parse arguments ──
  if (argc != 8) {
    if (rank == 0) Usage(argv[0]);
#ifdef USE_MPI
    MPI_Finalize();
#endif
    return 1;
  }

  size_t n1 = std::stoul(argv[1]) + 2 * kHalfLength;
  size_t n2 = std::stoul(argv[2]) + 2 * kHalfLength;
  size_t n3_user = std::stoul(argv[3]);
  size_t n1_block = std::stoul(argv[4]);
  size_t n2_block = std::stoul(argv[5]);
  size_t n3_block = std::stoul(argv[6]);
  size_t num_iterations = std::stoul(argv[7]);

  if (!ValidateInput(n1, n2, n3_user + 2 * kHalfLength,
                     n1_block, n2_block, n3_block, num_iterations)) {
#ifdef USE_MPI
    MPI_Finalize();
#endif
    return 1;
  }

  // ── Domain decomposition ──
  size_t n3_local, z_start_global, n3_alloc;
#ifdef USE_MPI
  n3_local = n3_user / nprocs;
  size_t remainder = n3_user % nprocs;
  if (rank < (int)remainder) n3_local++;
  z_start_global = rank * (n3_user / nprocs)
                 + std::min((size_t)rank, remainder);
  n3_alloc = n3_local + 2 * kHalfLength;
#else
  n3_local = n3_user;
  z_start_global = 0;
  n3_alloc = n3_user + 2 * kHalfLength;
#endif

  // ── Allocate arrays ──
  size_t total_size = n1 * n2 * n3_alloc;
  float* prev_base = new float[total_size];
  float* next_base = new float[total_size];
  float* vel_base  = new float[total_size];

  // ── Coefficients ──
  float coeff[kHalfLength + 1];
  for (size_t i = 0; i <= kHalfLength; i++) {
    coeff[i] = stencil_coeff[i];
  }

  // ── Initialize ──
#ifdef USE_MPI
  InitializeLocal(prev_base, next_base, vel_base, n1, n2, n3_alloc,
                  z_start_global, n3_local, rank, nprocs);
#else
  Initialize(prev_base, next_base, vel_base, n1, n2, n3_alloc);
#endif

  // ── Run ──
#ifdef USE_MPI
  MPI_Barrier(MPI_COMM_WORLD);
#endif
  auto start = std::chrono::high_resolution_clock::now();

  Iso3dfd(next_base, prev_base, vel_base, coeff,
          n1, n2, n3_alloc,
          n1_block, n2_block, n3_block,
          num_iterations
#ifdef USE_MPI
          , rank, nprocs
#endif
          );

#ifdef USE_MPI
  MPI_Barrier(MPI_COMM_WORLD);
#endif
  auto end = std::chrono::high_resolution_clock::now();
  double time = std::chrono::duration<double>(end - start).count();

  // ── Stats ──
  double max_time = time;
#ifdef USE_MPI
  MPI_Reduce(&time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
#endif
  if (rank == 0) {
    PrintStats(max_time, n1, n2, n3_user + 2 * kHalfLength, num_iterations);
  }

  // ── Verify ──
#if defined(VERIFY_RESULTS)
#ifdef USE_MPI
  VerifyResults(next_base, prev_base, vel_base, coeff,
                n1, n2, n3_alloc, num_iterations,
                n1_block, n2_block, n3_block,
                rank, nprocs, n3_local, z_start_global, n3_user);
#else
  VerifyResults(next_base, prev_base, vel_base, coeff,
                n1, n2, n3_alloc, num_iterations,
                n1_block, n2_block, n3_block);
#endif
#endif

  // ── Cleanup ──
  delete[] prev_base;
  delete[] next_base;
  delete[] vel_base;

#ifdef USE_MPI
  MPI_Finalize();
#endif
  return 0;
}
