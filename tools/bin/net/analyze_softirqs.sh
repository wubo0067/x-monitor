#!/bin/bash

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <SOFTIRQ_NAME> [<SOFTIRQ_NAME> ...]"
    echo "Example: $0 NET_RX NET_TX"
    exit 1
fi

echo "=== SoftIRQ Analysis (Per-CPU, Per-IRQ) ==="
echo "Timestamp: $(date)"
echo ""

# 获取 CPU 核数
cpu_count=$(grep -c ^processor /proc/cpuinfo)

# 收集软中断值
declare -A irq_matrix  # key: irqname_cpu, value: count
irq_names=()

for irq_name in "$@"; do
    line=$(grep -w "$irq_name" /proc/softirqs)
    if [ -z "$line" ]; then
        echo "⚠️  SoftIRQ $irq_name not found!"
        continue
    fi
    irq_names+=("$irq_name")
    counts=$(echo "$line" | cut -d: -f2)

    i=0
    for count in $counts; do
        irq_matrix["$irq_name-$i"]=$count
        ((i++))
    done
done

# 打印表头
printf "%-8s" "CPU"
for irq in "${irq_names[@]}"; do
    printf "%12s" "$irq"
done
echo
echo "----------------------------------------------------------------------------"

# 输出每个 CPU 的所有中断值
for ((cpu=0; cpu<cpu_count; cpu++)); do
    printf "%-8s" "CPU$cpu"
    for irq in "${irq_names[@]}"; do
        val=${irq_matrix["$irq-$cpu"]}
        printf "%12s" "${val:-0}"
    done
    echo
done
