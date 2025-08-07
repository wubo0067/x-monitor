#!/bin/bash

# 设置参数
INTERVAL=3                      # 时间间隔（秒）
THRESHOLD=5                    # D 状态线程阈值
LOGFILE="/var/log/d_state_watch.log"  # 日志文件路径

# 创建日志文件（如果不存在）
touch "$LOGFILE"

echo "Starting D-state monitor. Logging to $LOGFILE"
echo "Threshold: $THRESHOLD threads in D state"

while true; do
    # 获取当前时间戳
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

    # 获取系统负载
    LOADAVG=$(cat /proc/loadavg | awk '{print $1" "$2" "$3}')

    # 统计 Running 状态线程数量
    RUNNING_COUNT=$(ps -eLo stat | awk '$1 ~ /^R/ {count++} END {print count+0}')

    # 输出当前系统状态到日志
    echo "[$TIMESTAMP] Load average: $LOADAVG, Running threads: $RUNNING_COUNT, D-state threads: $D_COUNT" >> "$LOGFILE"

    # 统计 D 状态线程数量（包括所有 TID）
    # 直接使用 ps 并通过 awk 进行精确匹配
    D_COUNT=$(ps -eLo stat | awk '$1 ~ /^D/ {count++} END {print count+0}')

    # 如果超过阈值，记录详细信息
    if [ "$D_COUNT" -ge "$THRESHOLD" ]; then
        echo "[$TIMESTAMP] D-state thread count: $D_COUNT exceeds threshold ($THRESHOLD)" >> "$LOGFILE"
        echo "[$TIMESTAMP] Capturing D-state thread details..." >> "$LOGFILE"
        ps -eLo pid,tid,psr,pcpu,stat,wchan:20,comm | awk '$5 ~ /^D/ {print}' >> "$LOGFILE"

        # 尝试获取 D 状态进程的堆栈信息
        echo "[$TIMESTAMP] D-state process stack details:" >> "$LOGFILE"

        # 获取所有 D 状态进程的 PID
        D_PIDS=$(ps -eLo pid,stat | awk '$2 ~ /^D/ {print $1}')

        # 遍历每个 D 状态进程
        for pid in $D_PIDS; do
            # 获取进程的基本信息
            PROCESS_INFO=$(ps -Lo pid,stat,wchan:30,comm -p $pid 2>/dev/null | tail -n +2)
            if [ -n "$PROCESS_INFO" ]; then
                echo "  $PROCESS_INFO" >> "$LOGFILE"
            else
                echo "  PID: $pid (进程已退出)" >> "$LOGFILE"
                continue
            fi

            # 尝试读取 /proc/pid/stack 文件获取堆栈信息
            if [ -r "/proc/$pid/stack" ]; then
                echo "    Stack trace:" >> "$LOGFILE"
                while IFS= read -r line; do
                    echo "      $line" >> "$LOGFILE"
                done < "/proc/$pid/stack" 2>/dev/null || echo "      (无法读取堆栈信息)" >> "$LOGFILE"
            else
                echo "    (无法访问 /proc/$pid/stack)" >> "$LOGFILE"
            fi
        done
        echo "------------------------------------------------------------" >> "$LOGFILE"
    else
        # 可选：即使未超过阈值也显示当前D状态线程数（用于调试）
        #echo "[$TIMESTAMP] D-state thread count: $D_COUNT (below threshold)" >> "$LOGFILE"
        true
    fi

    sleep "$INTERVAL"
done