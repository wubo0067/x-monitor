#!/bin/bash

set -x

COMMAND=$1

to_le_hex() {
    local width=$1 value=$2 hex result="" pos byte
    printf -v hex "%0${width}x" "$value"
    pos=${#hex}
    while (( pos > 0 )); do
        pos=$((pos - 2))
        byte=${hex:pos:2}
        result="${result:+$result }$byte"
    done
    echo "$result"
}

compose_map_value() {
    local rate_le=$1 zeros
    zeros=$(printf '00 %.0s' {1..115})
    zeros=${zeros% }
    echo "$rate_le 03 $zeros"
}

update_rate() {
    local cgroup_id=$1 rate_mbps=$2 key rate_le map_value
    if [[ -z "$cgroup_id" || -z "$rate_mbps" ]]; then
        echo "update_rate: missing arguments"
        return 1
    fi
    if ! [[ "$cgroup_id" =~ ^[0-9]+$ ]]; then
        echo "update_rate: invalid cgroup_id $cgroup_id"
        return 1
    fi
    if ! [[ "$rate_mbps" =~ ^[0-9]+$ ]]; then
        echo "update_rate: invalid rate $rate_mbps"
        return 1
    fi
    key=$(to_le_hex 16 "$cgroup_id")
    rate_le=$(to_le_hex 8 "$rate_mbps")
    map_value=$(compose_map_value "$rate_le")
    echo "Updating rate for cgroup ID $cgroup_id to ${rate_mbps}Mbps"
    echo "bpftool map update name xm_hbm_edt_stat key hex $key value hex $map_value"
    bpftool map update name xm_hbm_edt_stat key hex $key value hex $map_value
    if [ $? -ne 0 ]; then
        echo "update_rate: failed to update map"
        return 1
    fi
    echo "update_rate: map updated successfully"
    return 0
}

load_bpf() {
    local cgroup_path=$1
    local rate_mbps=$2
    local edt_bpf_path=$3

    if [ ! -f "$edt_bpf_path" ]; then
        echo "BPF file not found: $edt_bpf_path"
        return 1
    fi

    # stat -Lc %i $cgroup_path 获取 cgroup id
    cgroup_id=$(stat -Lc %i "$cgroup_path")
    echo "Cgroup ID for $cgroup_path is $cgroup_id"
    echo "Using BPF file: $edt_bpf_path"

    #使用命令bpftool prog load $edt_bpf_path /sys/fs/bpf/xm_hbm_edt type cgroup_skb/egress加载
    echo "bpftool prog load $edt_bpf_path /sys/fs/bpf/xm_hbm_edt type cgroup_skb/egress"
    bpftool prog load "$edt_bpf_path" /sys/fs/bpf/xm_hbm_edt type cgroup_skb/egress
    if [ $? -ne 0 ]; then
        echo "Failed to load $edt_bpf_path BPF program"
        return 1
    fi

    #更新map中的速率值
    if ! update_rate "$cgroup_id" "$rate_mbps"; then
        echo "Failed to update rate in BPF map"
        return 1
    fi

    #attach BPF program到cgroup
    echo "bpftool cgroup attach $cgroup_path cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt"
    bpftool cgroup attach "$cgroup_path" cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt
    if [ $? -ne 0 ]; then
        echo "Failed to attach BPF program to cgroup"
        return 1
    fi

    echo "Attaching BPF program to $cgroup_path with rate ${rate_mbps}Mbps"
    return 0
}

init() {
    if [ $# -ne 4 ]; then
        echo "Usage: $0 init <cgroup_path> <rate_mbps> <ifname> <edt_bpf_path>"
        exit 1
    fi

    cgroup_path=$1
    rate_mbps=$2
    ifname=$3
    edt_bpf_path=$4

    echo "Initializing HBM EDT with cgroup: $cgroup_path, rate: ${rate_mbps}Mbps, interface: $ifname, BPF: $edt_bpf_path"
    SHELL_PID=$$
    echo "Current shell PID: $SHELL_PID"

    if [ -d "$cgroup_path" ]; then
        echo "Cgroup path exists: $cgroup_path"
    else
        echo "Cgroup path does not exist, creating: $cgroup_path"
        mkdir -p "$cgroup_path"
        if [ $? -ne 0 ]; then
            echo "Failed to create cgroup path"
            exit 1
        fi
        echo "Cgroup path created successfully"
    fi

    echo "$SHELL_PID" > "$cgroup_path/cgroup.procs"
    if [ $? -ne 0 ]; then
        echo "Failed to add PID to cgroup.procs"
        exit 1
    fi

    if grep -q "^$SHELL_PID$" "$cgroup_path/cgroup.procs"; then
        echo "Successfully added PID $SHELL_PID to cgroup"
    else
        echo "Failed to verify PID in cgroup.procs"
        exit 1
    fi

    echo "Interface parameter received: $ifname"
    DEFAULT_QDISC=$(sysctl -n net.core.default_qdisc 2>/dev/null)
    TC_OUTPUT=$(tc qdisc show dev "$ifname" 2>/dev/null)
    ROOT_MQ=$(echo "$TC_OUTPUT" | awk '/^qdisc mq / {print "yes"; exit}')
    CHILD_FQ=$(echo "$TC_OUTPUT" | awk '/^qdisc fq / {print "yes"; exit}')

    if [ "$DEFAULT_QDISC" != "fq" ] || [ "$ROOT_MQ" != "yes" ] || [ "$CHILD_FQ" != "yes" ]; then
        if [ "$DEFAULT_QDISC" != "fq" ]; then
            echo "Setting net.core.default_qdisc to fq"
            if ! sysctl -w net.core.default_qdisc=fq >/dev/null 2>&1; then
                echo "Failed to set net.core.default_qdisc to fq"
                exit 1
            fi
        fi

        echo "Configuring mq + fq qdisc on $ifname"
        tc qdisc del dev "$ifname" root 2>/dev/null || true

        if ! tc qdisc replace dev "$ifname" root handle 1: mq; then
            echo "Failed to replace mq root qdisc on $ifname"
            exit 1
        fi
    else
        echo "$ifname already uses mq + fq qdisc configuration"
    fi

    if ! load_bpf "$cgroup_path" "$rate_mbps" "$edt_bpf_path"; then
        exit 1
    fi

    # bpftool cgroup list $cgroup_path
    bpftool cgroup list "$cgroup_path"

    echo "HBM EDT initialized successfully"
}

clean() {
    if [ $# -ne 1 ]; then
        echo "Usage: $0 clean <cgroup_path>"
        exit 1
    fi

    cgroup_path=$1
    echo "Cleaning up HBM EDT configuration for cgroup: $cgroup_path"

    # Detach BPF program from cgroup
    if [ -d "$cgroup_path" ]; then
        echo "bpftool cgroup detach $cgroup_path cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt"
        bpftool cgroup detach "$cgroup_path" cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt 2>/dev/null || echo "Failed to detach BPF program or program not attached"
    else
        echo "Cgroup path does not exist: $cgroup_path"
    fi

    # Unpin BPF program
    echo "Unpinning BPF program"
    rm -f /sys/fs/bpf/xm_hbm_edt 2>/dev/null || echo "BPF program not pinned or failed to unpin"

    # 删除cgroup
    echo "Removing cgroup directory: $cgroup_path"
    rmdir "$cgroup_path" 2>/dev/null || echo "Failed to remove cgroup directory or it is not empty"

    echo "HBM EDT cleanup completed"
}

update() {
    if [ $# -ne 2 ]; then
        echo "Usage: $0 update <cgroup_path> <rate_mbps>"
        exit 1
    fi

    local cgroup_path=$1
    local rate_mbps=$2
    local cgroup_id

    echo "Updating HBM EDT with cgroup: $cgroup_path, rate: ${rate_mbps}Mbps"

    if [ ! -d "$cgroup_path" ]; then
        echo "Cgroup path does not exist: $cgroup_path"
        exit 1
    fi

    # stat -Lc %i $cgroup_path 获取 cgroup id
    cgroup_id=$(stat -Lc %i "$cgroup_path")
    echo "Cgroup ID for $cgroup_path is $cgroup_id"

    #更新map中的速率值
    if ! update_rate "$cgroup_id" "$rate_mbps"; then
        echo "Failed to update rate in BPF map"
        exit 1
    fi

    echo "Rate updated successfully"
}

usage() {
    echo "Usage: $0 {init <cgroup_path> <rate_mbps> <ifname> <edt_bpf_path>|clean <cgroup_path>|update <cgroup_path> <rate_mbps>}"
    echo "Commands:"
    echo "  init <cgroup_path> <rate_mbps> <ifname> <edt_bpf_path> - Initialize HBM EDT with specified cgroup, rate, interface, and BPF file"
    echo "  clean <cgroup_path>                                    - Clean up HBM EDT configuration for specified cgroup"
    echo "  update <cgroup_path> <rate_mbps>                       - Update HBM EDT rate for specified cgroup"
}

case $COMMAND in
    init)
        if [ $# -ne 5 ]; then
            usage
            exit 1
        fi
        shift
        init "$1" "$2" "$3" "$4"
        ;;
    clean)
        if [ $# -ne 2 ]; then
            usage
            exit 1
        fi
        shift
        clean "$1"
        ;;
    update)
        shift
        update "$@"
        ;;
    *)
        usage
        exit 1
        ;;
esac