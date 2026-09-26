cat > numa_wrapper.sh << 'EOF'
#!/bin/bash
RANK=${PMI_RANK:-${OMPI_COMM_WORLD_RANK:-0}}
NUMA_NODES=$(numactl --hardware | grep "available:" | awk '{print $2}')
NODE=$((RANK % NUMA_NODES))
exec numactl --cpunodebind=$NODE --membind=$NODE "$@"
EOF
chmod +x numa_wrapper.sh

