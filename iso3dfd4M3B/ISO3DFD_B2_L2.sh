#!/bin/bash
set -e

BINARY="./build/iso3dfd"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG="iso3dfd_b2_l2_tuning_${TIMESTAMP}.log"

echo "========================================" | tee "$LOG"
echo "iso3dfd V2 — tuning b2_l2" | tee -a "$LOG"
echo "Дата: $(date '+%Y-%m-%d %H:%M:%S')" | tee -a "$LOG"
echo "Сервер: $(hostname)" | tee -a "$LOG"
echo "Binary: $BINARY" | tee -a "$LOG"
echo "Тест: 256×256×256, 2000 итер, pure_omp 12" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"
echo "" | tee -a "$LOG"

# Очистка
unset I_MPI_PIN
unset I_MPI_PIN_DOMAIN
unset I_MPI_PIN_ORDER

export OMP_NUM_THREADS=12
export OMP_PROC_BIND=close
export OMP_PLACES=cores

for B2L2 in 32 40 48 56 64; do
    echo "========================================" | tee -a "$LOG"
    echo ">>> b2_l2=$B2L2" | tee -a "$LOG"
    echo "--- Окружение ---" | tee -a "$LOG"
    echo "OMP_NUM_THREADS=$OMP_NUM_THREADS" | tee -a "$LOG"
    echo "OMP_PROC_BIND=$OMP_PROC_BIND" | tee -a "$LOG"
    echo "OMP_PLACES=$OMP_PLACES" | tee -a "$LOG"
    echo "ISO3DFD_B2_L2=$B2L2" | tee -a "$LOG"
    echo "--- Команда ---" | tee -a "$LOG"
    echo "ISO3DFD_B2_L2=$B2L2 $BINARY 256 256 256 64 64 64 2000 pure_omp" | tee -a "$LOG"
    echo "--- Вывод ---" | tee -a "$LOG"
    ISO3DFD_B2_L2=$B2L2 $BINARY 256 256 256 64 64 64 2000 pure_omp 2>&1 | tee -a "$LOG"
    echo "" | tee -a "$LOG"
done

echo "========================================" | tee -a "$LOG"
echo "Tuning завершён: $(date '+%Y-%m-%d %H:%M:%S')" | tee -a "$LOG"
echo "Лог: $LOG" | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"
