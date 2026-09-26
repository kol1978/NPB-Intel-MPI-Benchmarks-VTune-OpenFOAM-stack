#include "../include/iso3dfd.h"

void Usage(const std::string& programName) {
  std::cout << "--------------------------------------\n";
  std::cout << " Incorrect parameters \n";
  std::cout << " Usage: " << programName << " n1 n2 n3\n";
  std::cout << "         n1 n2 n3                       : Grid sizes for the stencil\n";
  std::cout << "         n1_block n2_block n3_block     : Cache block sizes for CPU\n";
  std::cout << "                                        : TILE sizes for OMP Offload\n";
}

void Initialize(float* ptr_prev, float* ptr_next, float* ptr_vel,
                size_t n1, size_t n2, size_t n3) {
  size_t total = n1 * n2 * n3;
  for (size_t i = 0; i < total; ++i) {
    ptr_prev[i] = 1.0f;
    ptr_next[i] = 0.0f;
    ptr_vel[i]  = 2250000.0f * dt * dt;
  }
}

#ifdef USE_MPI
void InitializeLocal(float* ptr_prev, float* ptr_next, float* ptr_vel,
                     size_t n1, size_t n2, size_t n3_alloc,
                     size_t z_start_global, size_t n3_local,
                     int rank, int nprocs) {
  size_t layer_size = n1 * n2;
  float vel_value = 2250000.0f * dt * dt;

  for (size_t iz = 0; iz < n3_alloc; ++iz) {
    size_t base_idx = iz * layer_size;
    for (size_t iy = 0; iy < n2; ++iy) {
      for (size_t ix = 0; ix < n1; ++ix) {
        size_t idx = base_idx + iy * n1 + ix;
        ptr_prev[idx] = 1.0f;
        ptr_next[idx] = 0.0f;
        ptr_vel[idx]  = vel_value;
      }
    }
  }
}
#endif

void PrintStats(double time, size_t n1, size_t n2, size_t n3,
                size_t num_iterations) {
  double cells_per_sec = (static_cast<double>(n1) * n2 * n3 * num_iterations) / time;
  std::cout << "\n=== Simulation Statistics ===\n";
  std::cout << "Grid size: " << n1 << " x " << n2 << " x " << n3 << "\n";
  std::cout << "Iterations: " << num_iterations << "\n";
  std::cout << "Time: " << time << " s\n";
  std::cout << "Performance: " << cells_per_sec / 1e9 << " GCells/s\n";
  std::cout << "============================\n";
}

bool WithinEpsilon(float* output, float* reference, size_t dim_x,
                   size_t dim_y, size_t dim_z, size_t radius,
                   const int zadjust, const float delta) {
  size_t total = dim_x * dim_y * dim_z;
  int count_bad = 0;
  float max_diff = 0.0f;

  size_t start_idx = radius * dim_x;
  size_t end_idx = total - radius * dim_x;

  for (size_t i = start_idx; i < end_idx; ++i) {
    float diff = std::fabs(output[i] - reference[i]);
    if (diff > max_diff) max_diff = diff;
    if (diff > delta) count_bad++;
  }

  if (count_bad > 0) {
    std::cerr << "WithinEpsilon: " << count_bad << " points outside tolerance (max diff="
              << max_diff << ", delta=" << delta << ")\n";
    return false;
  }
  return true;
}

bool ValidateInput(size_t n1, size_t n2, size_t n3,
                   size_t n1_block, size_t n2_block,
                   size_t n3_block, size_t num_iterations) {
  if (n1 <= 2 * kHalfLength || n2 <= 2 * kHalfLength || n3 <= 2 * kHalfLength) {
    std::cerr << "Error: Grid dimensions must be larger than 2 * kHalfLength ("
              << 2 * kHalfLength << ")\n";
    return false;
  }
  if (n1_block == 0 || n2_block == 0 || n3_block == 0) {
    std::cerr << "Error: Block sizes must be > 0\n";
    return false;
  }
  if (num_iterations == 0) {
    std::cerr << "Error: Number of iterations must be > 0\n";
    return false;
  }
  return true;
}
