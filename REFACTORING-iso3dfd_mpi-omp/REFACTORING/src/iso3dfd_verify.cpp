//==============================================================
// Copyright © 2020 Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================

#include "../include/iso3dfd.h"
#include <cstring>

#ifdef USE_MPI
#include <mpi.h>
#endif

// ── Stencil lookup for verify (uses local ptr_prev, ix, n1, dimn1n2) ──
#undef STENCIL_LOOKUP
#define STENCIL_LOOKUP(n) \
  value += ptr_prev[ix + n] * coeff[n] - ptr_prev[ix - n] * coeff[n]; \
  value += ptr_prev[ix + n * n1] * coeff[n] - ptr_prev[ix - n * n1] * coeff[n]; \
  value += ptr_prev[ix + n * dimn1n2] * coeff[n] - ptr_prev[ix - n * dimn1n2] * coeff[n]

void Iso3dfdVerifyIteration(float *ptr_next_base, float *ptr_prev_base,
                            float *ptr_vel_base, float *coeff,
                            size_t n1, size_t n2, size_t n3,
                            size_t n1_block, size_t n2_block,
                            size_t n3_block) {
  size_t dimn1n2 = n1 * n2;
  size_t n3_end = n3 - kHalfLength;
  size_t n2_end = n2 - kHalfLength;
  size_t n1_end = n1 - kHalfLength;

#pragma omp parallel default(shared)
#pragma omp for schedule(static) collapse(3)
  for (size_t bz = kHalfLength; bz < n3_end; bz += n3_block) {
    for (size_t by = kHalfLength; by < n2_end; by += n2_block) {
      for (size_t bx = kHalfLength; bx < n1_end; bx += n1_block) {
        size_t iz_end = std::min(bz + n3_block, n3_end);
        size_t iy_end = std::min(by + n2_block, n2_end);
        size_t ix_end = std::min(n1_block, n1_end - bx);
        for (size_t iz = bz; iz < iz_end; iz++) {
          for (size_t iy = by; iy < iy_end; iy++) {
            float *ptr_next = ptr_next_base + iz * dimn1n2 + iy * n1 + bx;
            float *ptr_prev = ptr_prev_base + iz * dimn1n2 + iy * n1 + bx;
            float *ptr_vel = ptr_vel_base + iz * dimn1n2 + iy * n1 + bx;
#pragma omp simd
            for (int ix = 0; ix < (int)ix_end; ix++) {
              float value = 0.0f;
              value += ptr_prev[ix] * coeff[0];
              value += STENCIL_LOOKUP(1);
              value += STENCIL_LOOKUP(2);
              value += STENCIL_LOOKUP(3);
              value += STENCIL_LOOKUP(4);
              value += STENCIL_LOOKUP(5);
              value += STENCIL_LOOKUP(6);
              value += STENCIL_LOOKUP(7);
              value += STENCIL_LOOKUP(8);
              ptr_next[ix] =
                  2.0f * ptr_prev[ix] - ptr_next[ix] + value * ptr_vel[ix];
            }
          }
        }
      }
    }
  }
}

void Iso3dfdVerify(float *ptr_next, float *ptr_prev, float *ptr_vel,
                   float *coeff, size_t n1, size_t n2,
                   size_t n3, size_t nreps, size_t n1_block,
                   size_t n2_block, size_t n3_block) {
  for (size_t it = 0; it < nreps; it += 1) {
    Iso3dfdVerifyIteration(ptr_next, ptr_prev, ptr_vel, coeff, n1, n2, n3,
                           n1_block, n2_block, n3_block);
    it++;
    if (it < nreps)
      Iso3dfdVerifyIteration(ptr_prev, ptr_next, ptr_vel, coeff, n1, n2, n3,
                             n1_block, n2_block, n3_block);
  }
}

#ifndef USE_MPI
// ═══════════════════════════════════════════════════════════════
// Non-MPI verification
// ═══════════════════════════════════════════════════════════════
bool VerifyResults(float *next_base, float *prev_base, float *vel_base,
                   float *coeff, size_t n1, size_t n2,
                   size_t n3, size_t num_iterations,
                   size_t n1_block, size_t n2_block,
                   size_t n3_block) {
  std::cout << "Checking Results ...\n";
  size_t nsize = n1 * n2 * n3;

  // Save the MPI-computed result
  float *temp = new float[nsize];
  if (num_iterations % 2)
    memcpy(temp, next_base, nsize * sizeof(float));
  else
    memcpy(temp, prev_base, nsize * sizeof(float));

  // Reinitialize and run serial reference
  Initialize(prev_base, next_base, vel_base, n1, n2, n3);
  Iso3dfdVerify(next_base, prev_base, vel_base, coeff, n1, n2, n3,
                num_iterations, n1_block, n2_block, n3_block);

  // Compare
  float *ref_result = (num_iterations % 2) ? next_base : prev_base;
  bool error = false;
  size_t count_bad = 0;
  float max_diff = 0.0f;

  for (size_t i = 0; i < nsize; i++) {
    float diff = std::fabs(temp[i] - ref_result[i]);
    if (diff > max_diff) max_diff = diff;
    if (diff > 0.1f) count_bad++;
  }

  if (count_bad > 0) {
    error = true;
    std::cout << "Verification: FAIL (" << count_bad
              << " points differ, max diff=" << max_diff << ")\n";
  } else {
    std::cout << "Verification: SUCCESS (max diff=" << max_diff << ")\n";
  }
  std::cout << "--------------------------------------\n";
  delete[] temp;
  return error;
}

#else
// ═══════════════════════════════════════════════════════════════
// MPI verification — gather interior data, compare on rank 0
// ═══════════════════════════════════════════════════════════════
bool VerifyResults(float *next_base, float *prev_base, float *vel_base,
                   float *coeff, size_t n1, size_t n2,
                   size_t n3, size_t num_iterations,
                   size_t n1_block, size_t n2_block,
                   size_t n3_block,
                   int rank, int nprocs,
                   size_t n3_local, size_t z_start_global,
                   size_t n3_user) {
  if (rank == 0)
    std::cout << "Checking Results (MPI, " << nprocs << " ranks) ...\n";

  size_t layer_size = n1 * n2;

  // Which buffer holds the final result?
  float *result_local = (num_iterations % 2) ? next_base : prev_base;

  // Interior data: skip bottom halo, take n3_local layers
  float *interior_local = result_local + kHalfLength * layer_size;
  int local_count = (int)(n3_local * layer_size);

  // Prepare gather buffers on rank 0
  int *recvcounts = nullptr;
  int *displs = nullptr;
  float *gathered = nullptr;
  size_t total_interior = n3_user * layer_size;

  if (rank == 0) {
    recvcounts = new int[nprocs];
    displs = new int[nprocs];
    gathered = new float[total_interior];
  }

  // Gather local counts
  MPI_Gather(&local_count, 1, MPI_INT,
             recvcounts, 1, MPI_INT, 0, MPI_COMM_WORLD);

  // Gather z_start_global to compute displacements
  unsigned long z_start_ul = (unsigned long)z_start_global;
  unsigned long *z_starts = nullptr;
  if (rank == 0) z_starts = new unsigned long[nprocs];
  MPI_Gather(&z_start_ul, 1, MPI_UNSIGNED_LONG,
             z_starts, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    for (int r = 0; r < nprocs; r++) {
      displs[r] = (int)(z_starts[r] * layer_size);
    }
  }

  // Gather interior data from all ranks
  MPI_Gatherv(interior_local, local_count, MPI_FLOAT,
              gathered, recvcounts, displs, MPI_FLOAT,
              0, MPI_COMM_WORLD);

  bool error = false;

  if (rank == 0) {
    // Run serial reference on the full grid
    size_t n3_full = n3_user + 2 * kHalfLength;
    float *ref_prev = new float[n1 * n2 * n3_full];
    float *ref_next = new float[n1 * n2 * n3_full];
    float *ref_vel  = new float[n1 * n2 * n3_full];

    Initialize(ref_prev, ref_next, ref_vel, n1, n2, n3_full);
    Iso3dfdVerify(ref_next, ref_prev, ref_vel, coeff, n1, n2, n3_full,
                  num_iterations, n1_block, n2_block, n3_block);

    // Reference interior: skip halos
    float *ref_result = (num_iterations % 2) ? ref_next : ref_prev;
    float *ref_interior = ref_result + kHalfLength * layer_size;

    // Compare
    size_t count_bad = 0;
    float max_diff = 0.0f;
    for (size_t i = 0; i < total_interior; i++) {
      float diff = std::fabs(gathered[i] - ref_interior[i]);
      if (diff > max_diff) max_diff = diff;
      if (diff > 0.1f) count_bad++;
    }

    if (count_bad > 0) {
      error = true;
      std::cout << "Verification: FAIL (" << count_bad
                << " points differ, max diff=" << max_diff << ")\n";
    } else {
      std::cout << "Verification: SUCCESS (max diff=" << max_diff << ")\n";
    }
    std::cout << "--------------------------------------\n";

    delete[] ref_prev;
    delete[] ref_next;
    delete[] ref_vel;
  }

  // Broadcast result to all ranks
  int error_int = error ? 1 : 0;
  MPI_Bcast(&error_int, 1, MPI_INT, 0, MPI_COMM_WORLD);
  error = (error_int != 0);

  // Cleanup
  if (rank == 0) {
    delete[] recvcounts;
    delete[] displs;
    delete[] gathered;
    delete[] z_starts;
  }

  return error;
}
#endif
