#!/bin/bash
export I_MPI_PIN=1
export I_MPI_PIN_DOMAIN=node0

RANK=${PMI_RANK:-${I_MPI_RANK:-0}}

if [ "$RANK" = "0" ]; then
    exec vtune -collect hotspots \
         -knob sampling-mode=sw \
         -result-dir r000hs_mpi \
         -- "$@"
else
    exec "$@"
fi
