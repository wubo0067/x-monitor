#!/bin/bash

echo "===== MLX5 Queue Interrupt Affinity Info ====="
echo "Timestamp: $(date)"
echo ""

# 遍历 /proc/interrupts 找到所有 mlx5_comp 相关的中断
grep -E "mlx5_comp[0-9]+" /proc/interrupts | while read -r line; do
    # 提取中断号
    irq_num=$(echo "$line" | awk -F: '{print $1}' | xargs)
    # 提取设备名（mlx5_compX）
    dev_name=$(echo "$line" | grep -o "mlx5_comp[0-9]\+")
    # 提取每个 CPU 上的触发次数
    irq_counts=$(echo "$line" | awk -F: '{print $2}' | cut -d' ' -f2-)

    # 获取 smp_affinity_list 显示绑定的 CPU 核号
    affinity_file="/proc/irq/$irq_num/smp_affinity_list"
    if [ -f "$affinity_file" ]; then
        affinity=$(cat "$affinity_file")
    else
        affinity="N/A"
    fi

    echo "[$dev_name] IRQ: $irq_num"
    echo "  → IRQ counts per CPU : $irq_counts"
    echo "  → CPU affinity list  : $affinity"
    echo ""
done
