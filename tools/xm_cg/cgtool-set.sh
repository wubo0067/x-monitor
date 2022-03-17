#!/bin/bash


current_user=$(whoami)
if [ "$current_user" != "root" ]; then
        has_root_permission=$(sudo -l -U $current_user | grep "(root) ALL")
        if [ -n "$has_root_permission" ]; then
                echo "User $current_user has sudo permission"
                prefix="sudo"
        else
                echo "User $current_user has no permission to execute this script!"
                exit 1
        fi
fi

# 使用libcgroup-tools来控制进程组资源
cgcreate -g memory:/x-monitor/test
# 限制内存使用
cgset -r memory.limit_in_bytes=10M /x-monitor/test
# 关闭swap
cgset -r memory.swappiness=0 /x-monitor/test
# 查看当前mem-cgroup设置
cgget -g memory:/x-monitor/test
# 查看设置文件
cat /sys/fs/cgroup/memory/x-monitor/test/memory.limit_in_bytes
# 使用设置的内存cgroup来运行
cgexec -g memory:/x-monitor/test python3
