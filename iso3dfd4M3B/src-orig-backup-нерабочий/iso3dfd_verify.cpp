#ifdef VERIFY_RESULTS

#include "iso3dfd.h"
#include <cstdio>
#include <cmath>

// -----------------------------------------------------------------------
// Простая верификация: проверка на NaN/Inf и базовая статистика
// -----------------------------------------------------------------------
extern "C" void verify_result(const float* ptr, int n1, int n2, int n3,
                               int half_length) {
    float min_val = 1e30f, max_val = -1e30f;
    int nan_count = 0;
    int inf_count = 0;

    for (int z = half_length; z < n3 - half_length; z++) {
        for (int y = half_length; y < n2 - half_length; y++) {
            for (int x = half_length; x < n1 - half_length; x++) {
                int idx = (z * n2 + y) * n1 + x;
                float v = ptr[idx];
                if (std::isnan(v)) nan_count++;
                if (std::isinf(v)) inf_count++;
                if (v < min_val) min_val = v;
                if (v > max_val) max_val = v;
            }
        }
    }

    if (nan_count > 0 || inf_count > 0) {
        printf("VERIFY FAILED: %d NaN, %d Inf\n", nan_count, inf_count);
    } else {
        printf("VERIFY OK: min=%.6f, max=%.6f\n", min_val, max_val);
    }
}

#endif // VERIFY_RESULTS
