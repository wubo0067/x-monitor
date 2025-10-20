#!/bin/bash

# enable debug output for each executed command, to disable: set +x
set -x

# exit if any command fails
# set -e

# Function to initialize
init() {
    echo "Initializing..."

    # 获取当前终端的 shell PID
    SHELL_PID=$(ps -o ppid= -p $$)
    SHELL_PID=$(echo $SHELL_PID | tr -d ' ')
    echo "Script PID: $$, Shell PID: $SHELL_PID"

    # Check if /tmp/cgroupv2 exists and is mounted with cgroup2
    if mountpoint -q /tmp/cgroupv2 && mount | grep -q "/tmp/cgroupv2.*cgroup2"; then
        echo "/tmp/cgroupv2 already exists and is mounted with cgroup2"
        echo $PPID | sudo tee -a /tmp/cgroupv2/foo/cgroup.procs > /dev/null
        if [ $? -eq 0 ]; then
            echo "Successfully added parent PID $PPID to cgroup"
        else
            echo "Failed to add parent PID $PPID to cgroup"
        fi

        # 验证
        if grep -q "$PPID" /tmp/cgroupv2/foo/cgroup.procs; then
            echo "Parent PID $PPID found in cgroup.procs"
        else
            echo "Parent PID $PPID NOT found in cgroup.procs"
        fi
        return 0
    fi
    rm -rf /tmp/cgroupv2
    mkdir -p /tmp/cgroupv2
    mount -t cgroup2 none /tmp/cgroupv2
    mkdir -p /tmp/cgroupv2/foo
    # stat -Lc %i /tmp/cgroupv2/foo 获得cgroupid
    #当前shell加入cgroup
    echo $SHELL_PID | sudo tee -a /tmp/cgroupv2/foo/cgroup.procs > /dev/null
}

# Function to clean
clean() {
    echo "Cleaning..."
	umount /tmp/cgroupv2
	rm -rf /tmp/cgroupv2
}

# Function to load
load() {
    echo "Loading..."
    bpftool prog load .output/xm_sockops_redir.bpf.o /sys/fs/bpf/xm_sockops_redir type sockops
    # cgroup_sock_ops这是enum bpf_attach_type中的一种
    bpftool cgroup attach /tmp/cgroupv2/foo cgroup_sock_ops pinned /sys/fs/bpf/xm_sockops_redir

    #从xm_sockops_redir中提取mapid
    MAP_ID=$(sudo bpftool prog show pinned /sys/fs/bpf/xm_sockops_redir | grep -o -E 'map_ids [0-9]+' | cut -d ' ' -f2-)
    bpftool map pin id $MAP_ID  /sys/fs/bpf/xm_sock_redir_hash

    # 让 xm_sockmsg_redir 也使用这个 map
    bpftool prog load .output/xm_sockmsg_redir.bpf.o  /sys/fs/bpf/xm_sockmsg_redir type sk_msg map name xm_sock_redir_hash pinned /sys/fs/bpf/xm_sock_redir_hash
    # msg_verdict这是enum bpf_attach_type中的一种
    bpftool prog attach pinned /sys/fs/bpf/xm_sockmsg_redir msg_verdict pinned /sys/fs/bpf/xm_sock_redir_hash
}

# Function to unload
unload() {
    echo "Unloading..."
    bpftool prog detach pinned /sys/fs/bpf/xm_sockmsg_redir msg_verdict pinned /sys/fs/bpf/xm_sock_redir_hash
    rm /sys/fs/bpf/xm_sockmsg_redir

    bpftool cgroup detach  /tmp/cgroupv2/foo cgroup_sock_ops pinned /sys/fs/bpf/xm_sockops_redir
    rm /sys/fs/bpf/xm_sockops_redir

    rm /sys/fs/bpf/xm_sock_redir_hash
}

# Check command line arguments
case "$1" in
    load)
        load
        ;;
    unload)
        unload
        ;;
    init)
        init
        ;;
    clean)
        clean
        ;;
    *)
        echo "Usage: $0 {load|unload|init|clean}"
        exit 1
        ;;
esac