#!/bin/bash

# 检查 cgroup 版本
check_cgroup_version() {
  if mount | grep -q "cgroup2"; then
    echo "cgroup V2"
  else
    echo "cgroup V1"
  fi
}

# 调用函数并将返回值存储在变量中
cgroup_version=$(check_cgroup_version)

if [[ "$cgroup_version" == "cgroup V1" ]]; then
    echo "system cgroup version is v1"
    cgcreate -g memory:/user.slice/hello-cg -g cpu:/user.slice/hello-cg
    # 限制内存使用为1G
    cgset -r memory.limit_in_bytes=1024M /user.slice/hello-cg
    cgset -r memory.soft_limit_in_bytes=1024M /user.slice/hello-cg
    # 限制cpu的使用为1core
    cgset -r cpu.cfs_period_us=100000 /user.slice/hello-cg
    cgset -r cpu.cfs_quota_us=100000 /user.slice/hello-cg

    cgget -g memory:/user.slice/hello-cg
    cgget -g cpu:/user.slice/hello-cg
elif [[ "$cgroup_version" == "cgroup V2" ]]; then
    echo "system cgroup version is v2"
    cgcreate -g memory,cpu:/user.slice/hello-cg
    # 限制cpu的使用为1core
    cgxset -2 -r cpu.max='50000 100000' /user.slice/hello-cg
    # 限制memory上限，1G
    cgxset -2 -r memory.max=1073741824 /user.slice/hello-cg
    # 不使用swap
    cgxset -2 -r memory.swap.max=0 /user.slice/hello-cg

    cgxget -g memory,cpu:/user.slice/hello-cg
else
    echo "system cgroup version is unknown"
fi


