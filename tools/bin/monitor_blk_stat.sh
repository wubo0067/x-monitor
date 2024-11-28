#!/bin/bash

# Name            units         description
# ----            -----         -----------
# read I/Os       requests      number of read I/Os processed
# read merges     requests      number of read I/Os merged with in-queue I/O
# read sectors    sectors       number of sectors read
# read ticks      milliseconds  total wait time for read requests
# 这些值计算了I/O请求在该块设备上等待的毫秒数。如果有多个I/O请求等待，这些值将以大于1000/秒的速率增加；
# 例如，如果60个读请求平均等待了30毫秒，read_ticks字段将增加60*30=1800。

# write I/Os      requests      number of write I/Os processed
# write merges    requests      number of write I/Os merged with in-queue I/O
# write sectors   sectors       number of sectors written
# write ticks     milliseconds  total wait time for write requests
# in_flight       requests      number of I/Os currently in flight
# 这个值统计了已经发送给设备驱动程序但尚未完成的I/O请求的数量。它不包括还在队列中尚未发送给设备驱动程序的I/O请求。

# io_ticks        milliseconds  total time this block device has been active
# 这个值计算设备有I/O请求排队的毫秒数。

# time_in_queue   milliseconds  total wait time for all requests
# 这个值计算了I/O请求在这个块设备上等待的毫秒数。如果有多个I/O请求在等待，这个值会增加，作为等待的毫秒数与请求数量的乘积（参见上面的“读时钟”作为例子）。

# discard I/Os    requests      number of discard I/Os processed
# discard merges  requests      number of discard I/Os merged with in-queue I/O
# discard sectors sectors       number of sectors discarded
# discard ticks   milliseconds  total wait time for discard requests

# 检查参数数量
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <device_list> <interval>"
    echo "Example: $0 dm-0,dm-1 1"
    exit 1
fi

# 解析参数
device_list=$1
interval=$2

# 将设备名列表转换为数组
IFS=',' read -r -a devices <<< "$device_list"

# 检查间隔时间是否为正数
if ! [[ "$interval" =~ ^[0-9]+([.][0-9]+)?$ ]] || (( $(echo "$interval <= 0" | bc -l) )); then
    echo "Interval must be a positive number."
    exit 1
fi

# 打印表头
echo "Timestamp,Device,read I/Os,read merges,read sectors,read ticks,write I/Os,write merges,write sectors,write ticks,in_flight,io_ticks,time_in_queue,discard I/Os,discard merges,discard sectors,discard ticks"

# 循环读取并输出设备统计信息
while true; do
    timestamp=$(date +"%Y-%m-%d %H:%M:%S")
    for device in "${devices[@]}"; do
        stat_file="/sys/block/$device/stat"
        if [ -f "$stat_file" ]; then
            # 读取 stat 文件内容
            read -r reads read_merges read_sectors read_time writes write_merges write_sectors write_time ios_in_progress io_time weighted_io_time discard discard_merges discard_sectors discard_time < "$stat_file"
            # 输出统计信息
            echo "$timestamp,$device,$reads,$read_merges,$read_sectors,$read_time,$writes,$write_merges,$write_sectors,$write_time,$ios_in_progress,$io_time,$weighted_io_time,$discard,$discard_merges,$discard_sectors,$discard_time"
        else
            echo "Device $device not found."
        fi
    done
    # 等待指定间隔时间
    sleep "$interval"
done