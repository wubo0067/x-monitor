#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2025 CalmWu

# This script is used ftrace to trace a specific task by command
# Like trace-cmd record -p function -F <command>

# 获取执行的命令，脚本后面紧跟的就是要执行的命令
if [ $# -eq 0 ]; then
    echo "Usage: $0 <command>"
    exit 1
fi

CMD="$*"
# 输出命令
echo "Tracing command: $CMD"

#开始配置ftrace
echo "Configuring ftrace..."
# 清除之前的跟踪
echo > /sys/kernel/debug/tracing/trace
echo 0 > /sys/kernel/debug/tracing/tracing_on
echo 1 > /sys/kernel/debug/tracing/events/syscalls/enable
echo 1 > /sys/kernel/debug/tracing/exception/enable
echo 1 > /sys/kernel/debug/tracing/options/sym-addr
#跟踪fork和exit事件
echo 1 > /sys/kernel/debug/tracing/opttions/event-fork
#设置命令执行的pid
echo $$ > /sys/kernel/debug/traceing/set_event_pid
echo 1 > /sys/kernel/debug/tracing/tracing_on; $CMD; echo 0 > /sys/kernel/debug/tracing/tracing_on
# 清理ftrace设置
echo 0 > /sys/kernel/debug/tracing/events/syscalls/enable
echo 0 > /sys/kernel/debug/tracing/exception/enable
echo 0 > /sys/kernel/debug/tracing/opttions/event-fork
echo > /sys/kernel/debug/tracing/set_event_pid
cat /sys/kernel/debug/tracing/trace