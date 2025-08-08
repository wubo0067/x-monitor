#!/bin/bash

# 设置参数
INTERVAL=3                      # 时间间隔（秒）

 # loadavg 1分钟值的跳变阈值, 内核该值的计算有平滑因子，例如r+d增加5，该值才增加0.3-0.4
 # 合理的设置该值，感知跳变，避免无法抓取十分重要
JUMP_THRESHOLD=1
LOGFILE="/var/log/load_watch.log"  # 日志文件路径

# 创建日志文件（如果不存在）
touch "$LOGFILE"

echo "Starting D-state monitor. Logging to $LOGFILE"
echo "Jump threshold: $JUMP_THRESHOLD for loadavg 1-minute value"

# 初始化前一个loadavg值
PREV_LOADAVG=""

while true; do
    # 获取当前时间戳
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

    # 获取系统负载
    LOADAVG=$(cat /proc/loadavg | awk '{print $1" "$2" "$3}')

    # 获取1分钟loadavg值
    CURRENT_LOADAVG_1MIN=$(echo $LOADAVG | awk '{print $1}')

    # 统计 Running 状态线程数量
    RUNNING_COUNT=$(ps -eLo stat | awk '$1 ~ /^R/ {count++} END {print count+0}')

    # 统计 D 状态线程数量（包括所有 TID）
    # 直接使用 ps 并通过 awk 进行精确匹配
    D_COUNT=$(ps -eLo stat | awk '$1 ~ /^D/ {count++} END {print count+0}')

    # 输出当前系统状态到日志
    echo "[$TIMESTAMP] Load average: $LOADAVG, Running threads: $RUNNING_COUNT, D-state threads: $D_COUNT" >> "$LOGFILE"

    # 如果有前一个loadavg值，则比较跳变
    if [ -n "$PREV_LOADAVG" ]; then
        # 计算loadavg 1分钟值的增长量
        LOAD_INCREASE=$(echo "$CURRENT_LOADAVG_1MIN $PREV_LOADAVG" | awk '{print $1-$2}')

        # 检查是否超过跳变阈值
        JUMP_DETECTED=$(echo "$LOAD_INCREASE $JUMP_THRESHOLD" | awk '{print ($1 >= $2)}')

        if [ "$JUMP_DETECTED" = "1" ]; then
            echo "[$TIMESTAMP] Loadavg 1-minute jump detected: $CURRENT_LOADAVG_1MIN (prev: $PREV_LOADAVG, increase: $LOAD_INCREASE, threshold: $JUMP_THRESHOLD)" >> "$LOGFILE"
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
        fi
    fi

    # 更新前一个loadavg值
    PREV_LOADAVG=$CURRENT_LOADAVG_1MIN

    sleep "$INTERVAL"
done