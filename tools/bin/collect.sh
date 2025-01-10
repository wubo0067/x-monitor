#!/bin/bash

export LANG=en.Us

#采集资源使用情况
collect_resource(){

    dir_path="/var/log/monitor-$(date +"%Y%m%d")"
    #检查目录是否存在
    [ ! -d "${dir_path}" ] && mkdir -p "${dir_path}"
    cd $dir_path
    
    current_date=$(date +"%y%m%d-%H%M%S")
    echo "Timestamp: ${current_date}"
    top -i1bHn1 > "${dir_path}/top-${current_date}.log"
    pidstat -u 1 1 > "${dir_path}/pidstat-${current_date}.log"
    mpstat -P ALL 1 2 > "${dir_path}/mpstat-${current_date}.log"
    sar -A >"${dir_path}/sar-${current_date}.log"

    perf stat -o perf-stat.log -- sleep 10
    perf record -agF 199 -o perf-record_all.data -- sleep 10
    perf record -e sched:sched_process_exec -a -o perf-process.data -- sleep 10
    perf record -e sched:sched_switch -a -g -o perf-switch.data -- sleep 10
    perf sched record -aT -o perf-sched.data -- sleep 10
    perf sched latency -s max -i perf-sched.data > perf-sched.log

}

#检测soft lockup事件
check_softlockup(){
    # 配置日志文件路径和soft lockup的关键词
    LOG_FILE="/var/log/messages" # 根据实际情况调整日志文件路径
    SOFT_LOCKUP_KEYWORD="soft lockup - CPU"

    # 检查是否存在日志文件
    if [ ! -f "$LOG_FILE" ]; then
        echo "日志文件不存在: $LOG_FILE"
        exit 1
    fi
    while true; do

        # 执行sa1命令生成sar日志
        /usr/lib64/sa/sa1 1 1

        # 检查是否存在soft lockup事件
        if grep -q "$SOFT_LOCKUP_KEYWORD" "$LOG_FILE"; then
            echo "检测到soft lockup事件,收集中..."
            collect_resource
            echo "soft lockup事件已收集完成。"
            exit 0
        else
            echo "未检测到soft lockup事件。"
        fi

        # 每30秒执行一次        
        sleep 30
    done
}

check_softlockup
