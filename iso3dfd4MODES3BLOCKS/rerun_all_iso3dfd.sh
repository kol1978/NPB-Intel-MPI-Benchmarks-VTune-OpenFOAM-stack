#!/bin/bash
# ============================================================
# iso3dfd — полный перезапуск всех 10 конфигураций
# Сервер: kol-serv, 27 сентября 2026 г.
# Сборка: build-vtune-hotspots/iso3dfd
# ============================================================

set -e
cd "$(dirname "$0")"

LOG="iso3dfd_full_rerun_$(date +%Y%m%d_%H%M%S).log"
BIN="./build-vtune-hotspots/iso3dfd"

echo "========================================" | tee "$LOG"
echo "iso3dfd — Full Rerun" | tee -a "$LOG"
echo "Date: $(date '+%Y-%m-%d %H:%M:%S')" | tee -a "$LOG"
echo "Binary: $BIN" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"

run_test() {
    local label="$1"
    shift
    echo "" | tee -a "$LOG"
    echo "--- $label ---" | tee -a "$LOG"
    echo "" | tee -a "$LOG"
    echo "[env] OMP_NUM_THREADS=$OMP_NUM_THREADS" | tee -a "$LOG"
    echo "[env] OMP_PROC_BIND=$OMP_PROC_BIND" | tee -a "$LOG"
    echo "[env] OMP_PLACES=$OMP_PLACES" | tee -a "$LOG"
    echo "[env] I_MPI_PIN=$I_MPI_PIN" | tee -a "$LOG"
    echo "[env] I_MPI_PIN_DOMAIN=$I_MPI_PIN_DOMAIN" | tee -a "$LOG"
    echo "[env] I_MPI_PIN_ORDER=$I_MPI_PIN_ORDER" | tee -a "$LOG"
    echo "" | tee -a "$LOG"
    echo "\$ $@" | tee -a "$LOG"
    echo "" | tee -a "$LOG"
    "$@" 2>&1 | tee -a "$LOG"
    echo "" | tee -a "$LOG"
    echo "--- end $label ---" | tee -a "$LOG"
}

# ============================================================
# Серия 1: исходные параметры (блок 16×8×64, 100 итераций)
# ============================================================

# --- Запуск 1: sequential ---
export OMP_NUM_THREADS=1
unset OMP_PROC_BIND OMP_PLACES I_MPI_PIN I_MPI_PIN_DOMAIN I_MPI_PIN_ORDER
run_test "Запуск 1: sequential" "$BIN" 256 256 256 16 8 64 100 sequential

# --- Запуск 2: pure_mpi 12 (n3=480) ---
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact
unset OMP_PROC_BIND OMP_PLACES
run_test "Запуск 2: pure_mpi 12 (n3=480)" mpirun -n 12 "$BIN" 256 256 480 16 8 40 100 pure_mpi

# --- Запуск 3: pure_mpi 2 (n3=256) ---
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact
unset OMP_PROC_BIND OMP_PLACES
run_test "Запуск 3: pure_mpi 2 (n3=256)" mpirun -n 2 "$BIN" 256 256 256 16 8 128 100 pure_mpi

# --- Запуск 4: hybrid 2×6 (256³) ---
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact
run_test "Запуск 4: hybrid 2×6 (256³)" mpirun -n 2 "$BIN" 256 256 256 16 8 128 100 hybrid

# --- Запуск 5: pure_omp 12 (256³) ---
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores
unset I_MPI_PIN I_MPI_PIN_DOMAIN I_MPI_PIN_ORDER
run_test "Запуск 5: pure_omp 12 (256³)" "$BIN" 256 256 256 16 8 64 100 pure_omp

# --- Запуск 6: pure_mpi 12 (n3=960) ---
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact
unset OMP_PROC_BIND OMP_PLACES
run_test "Запуск 6: pure_mpi 12 (n3=960)" mpirun -n 12 "$BIN" 256 256 960 16 8 80 100 pure_mpi

# --- Запуск 7: hybrid 2×6 (512×512×256) ---
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact
run_test "Запуск 7: hybrid 2×6 (512×512×256)" mpirun -n 2 "$BIN" 512 512 256 16 8 128 100 hybrid

# --- Запуск 0 (деградированный): pure_mpi 12 (n3=240) ---
export OMP_NUM_THREADS=1
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact
unset OMP_PROC_BIND OMP_PLACES
run_test "Запуск 0: pure_mpi 12 (n3=240, деградированный)" mpirun -n 12 "$BIN" 256 256 240 16 8 20 100 pure_mpi

# ============================================================
# Серия 2: адаптированный код (блок 64×64×64, 2000 итераций)
# ============================================================

# --- Запуск 8: hybrid 1×8 (n3=480) ---
export OMP_NUM_THREADS=8
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFF
export I_MPI_PIN_ORDER=compact
run_test "Запуск 8: hybrid 1×8 (n3=480, адаптированный)" mpirun -np 1 "$BIN" 256 256 480 64 64 64 2000 hybrid

# --- Запуск 9: hybrid 1×12 (n3=480) ---
export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0xFFF
export I_MPI_PIN_ORDER=compact
run_test "Запуск 9: hybrid 1×12 (n3=480, адаптированный)" mpirun -np 1 "$BIN" 256 256 480 64 64 64 2000 hybrid

# --- Запуск 10: hybrid 2×6 (n3=480) ---
export OMP_NUM_THREADS=6
export OMP_PROC_BIND=close
export OMP_PLACES=cores
export I_MPI_PIN=on
export I_MPI_PIN_DOMAIN=0x3F,0xFC0
export I_MPI_PIN_ORDER=compact
run_test "Запуск 10: hybrid 2×6 (n3=480, адаптированный)" mpirun -np 2 "$BIN" 256 256 480 64 64 64 2000 hybrid

# ============================================================
echo "" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"
echo "All tests completed: $(date '+%Y-%m-%d %H:%M:%S')" | tee -a "$LOG"
echo "Log saved: $LOG" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"
