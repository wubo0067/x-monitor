#!/bin/bash

# 检查参数
if [ $# -ne 1 ]; then
    echo "Usage: $0 <interval_seconds>"
    exit 1
fi

interval=$1

# 无限循环，直到 Ctrl+C
while true; do
    echo "==== $(date '+%Y-%m-%d %H:%M:%S') ===="

    # 打印标题行
    printf "%-6s %-12s %-10s %-14s %-10s %-16s %-10s\n" \
        "CPU" "processed" "dropped" "time_squeeze" "rcv_rps" "flow_limit_count" "sn_backlog"

    cpu_idx=0
    while read -r line; do
        fields=($line)
        num_fields=${#fields[@]}

        processed=$((16#${fields[0]}))
        dropped=$((16#${fields[1]}))
        time_squeeze=$((16#${fields[2]}))
        rcv_rps=$((16#${fields[9]}))
        flow_limit_count=$((16#${fields[10]}))

        if [ "$num_fields" -ge 13 ]; then
            sn_backlog=$((16#${fields[11]}))
        else
            sn_backlog=0
        fi

        # 输出每个CPU一整行
        # 第一列是中断处理程序接收的帧数。
        # 第二列是因超过netdev_max_backlog而丢弃的帧数。
        # 第三列是ksoftirqd在仍有工作待处理时耗尽netdev_budget或CPU时间的次数。
        # 通过RPS(Receive Packet Steering)机制转发到该CPU处理的数据包数量
        # 当启用CONFIG_NET_FLOW_LIMIT时，记录达到流量限制阈值的流的数量
        # 该CPU网络软中断的待处理队列长度（输入队列+处理队列）
        printf "%-6s %-12d %-10d %-14d %-10d %-16d %-10d\n" \
            "CPU${cpu_idx}" "$processed" "$dropped" "$time_squeeze" "$rcv_rps" "$flow_limit_count" "$sn_backlog"

        ((cpu_idx++))
    done < /proc/net/softnet_stat

    echo ""
    sleep "$interval"
done
