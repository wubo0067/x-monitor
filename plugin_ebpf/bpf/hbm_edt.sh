#!/bin/bash

#set -x

COMMAND=$1

# 全局变量，cgroup2的默认路径
CGROUP2_PATH="/sys/fs/cgroup/xm_hbm_edt"

make_cgroup_path() {
    local cgroup_name="$1"
    if [ -z "$cgroup_name" ]; then
        echo "make_cgroup_path: missing cgroup_name" >&2
        return 1
    fi

    # strip any leading slashes so we always append to CGROUP2_PATH
    local name="${cgroup_name#/}"
    local full_path="$CGROUP2_PATH/$name"

    if [ -d "$full_path" ]; then
        echo "$full_path"
        return 0
    fi

    if [ -e "$full_path" ] && [ ! -d "$full_path" ]; then
        echo "make_cgroup_path: path exists and is not a directory: $full_path" >&2
        return 1
    fi

    mkdir -p "$full_path" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "make_cgroup_path: failed to create directory: $full_path" >&2
        return 1
    fi

    echo "$full_path"
    return 0
}

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
    local rate_le=$1 verbose=$2 zeros
    zeros=$(printf '00 %.0s' {1..123})
    zeros=${zeros% }
    if [ "$verbose" -eq 0 ]; then
        echo "$rate_le 03 $zeros"
    else
        echo "$rate_le 0b $zeros"
    fi
}

update_rate() {
    local cgroup_id=$1 rate_mbps=$2 verbose=$3 key rate_le map_value
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
    map_value=$(compose_map_value "$rate_le" "$verbose")
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
    local cgroup_full_path="$1"
    local rate_mbps="$2"
    local edt_bpf_path="$3"
    local cgroup_id

    if [ ! -f "$edt_bpf_path" ]; then
        echo "BPF file not found: $edt_bpf_path"
        return 1
    fi

    cgroup_id=$(stat -Lc %i "$cgroup_full_path" 2>/dev/null)
    if [ -z "$cgroup_id" ]; then
        echo "load_bpf: failed to get cgroup id for $cgroup_full_path"
        return 1
    fi

    echo "Cgroup full path: $cgroup_full_path"
    echo "Cgroup ID for $cgroup_full_path is $cgroup_id"
    echo "Using BPF file: $edt_bpf_path"

    if [ -e /sys/fs/bpf/xm_hbm_edt ]; then
        echo "/sys/fs/bpf/xm_hbm_edt already exists, skipping bpftool prog load"
    else
        echo "bpftool prog load $edt_bpf_path /sys/fs/bpf/xm_hbm_edt type cgroup_skb/egress"
        bpftool prog load "$edt_bpf_path" /sys/fs/bpf/xm_hbm_edt type cgroup_skb/egress
        if [ $? -ne 0 ]; then
            echo "Failed to load $edt_bpf_path BPF program"
            return 1
        fi
    fi

    if ! update_rate "$cgroup_id" "$rate_mbps" 0; then
        echo "Failed to update rate in BPF map"
        return 1
    fi

    echo "bpftool cgroup attach $cgroup_full_path cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt"
    bpftool cgroup attach "$cgroup_full_path" cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt
    if [ $? -ne 0 ]; then
        echo "Failed to attach BPF program to $cgroup_full_path"
        return 1
    fi

    bpftool cgroup list "$cgroup_full_path"

    echo "Attaching BPF program to $cgroup_full_path with rate ${rate_mbps}Mbps"
    return 0
}

init() {

    local cgroup_name=$1
    local rate_mbps=$2
    local ifname=$3
    local edt_bpf_path=$4
    local cgroup_full_path
    local shell_pid

    cgroup_full_path=$(make_cgroup_path "$cgroup_name")
    if [ $? -ne 0 ] || [ -z "$cgroup_full_path" ]; then
        echo "load_bpf: failed to get/create cgroup path for $cgroup_name"
        return 1
    fi

    echo "Initializing HBM EDT with cgroup: $cgroup_name, rate: ${rate_mbps}Mbps, interface: $ifname, BPF: $edt_bpf_path"
    shell_pid=$(ps -o ppid= -p $$)
    shell_pid=$(echo $shell_pid | tr -d ' ')
    echo "Script PID: $$, Shell PID: $shell_pid"

    if [ -d "$cgroup_full_path" ]; then
        echo "Cgroup path exists: $cgroup_full_path"
    else
        echo "Cgroup path does not exist, creating: $cgroup_full_path"
        mkdir -p "$cgroup_full_path"
        if [ $? -ne 0 ]; then
            echo "Failed to create cgroup path"
            exit 1
        fi
        echo "Cgroup path created successfully"
    fi

    echo "$shell_pid" > "$cgroup_full_path/cgroup.procs"
    if [ $? -ne 0 ]; then
        echo "Failed to add PID to cgroup.procs"
        exit 1
    fi

    if grep -q "^$shell_pid$" "$cgroup_full_path/cgroup.procs"; then
        echo "Successfully added PID $shell_pid to cgroup"
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

    if ! load_bpf "$cgroup_full_path" "$rate_mbps" "$edt_bpf_path"; then
        exit 1
    fi

    echo "HBM EDT initialized successfully"
}

unload() {

    local cgroup_name=$1
    local cgroup_id key_hex status=0
    local cgroup_full_path

    cgroup_full_path=$(make_cgroup_path "$cgroup_name")
    if [ $? -ne 0 ] || [ -z "$cgroup_full_path" ]; then
        echo "load_bpf: failed to get/create cgroup path for $cgroup_name"
        return 1
    fi

    cgroup_id=$(stat -Lc %i "$cgroup_full_path")
    if [ -z "$cgroup_id" ]; then
        echo "Failed to retrieve cgroup ID for $cgroup_name"
        exit 1
    fi

    echo "Detaching BPF program from $cgroup_full_path"
    echo "bpftool cgroup detach $cgroup_full_path cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt"
    if ! bpftool cgroup detach "$cgroup_full_path" cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt; then
        echo "Failed to detach BPF program from $cgroup_full_path"
        status=1
    fi

    #删除cgroup目录
    echo "rmdir $cgroup_full_path"
    rmdir "$cgroup_full_path" 2>/dev/null || echo "Failed to delete cgroup directory: $cgroup_full_path"

    key_hex=$(to_le_hex 16 "$cgroup_id")
    echo "Deleting map entry for cgroup ID $cgroup_id"
    echo "bpftool map delete name xm_hbm_edt_stat key hex $key_hex"
    if ! bpftool map delete name xm_hbm_edt_stat key hex $key_hex; then
        echo "Failed to delete map entry for cgroup ID $cgroup_id"
        status=1
    fi

    if [ $status -eq 0 ]; then
        echo "HBM EDT unloaded for $cgroup_name, Please manually remove cgroup directory: $cgroup_full_path"
    fi

    return $status
}

clean() {
    # Detach BPF programs from all cgroups under CGROUP2_PATH
    if [ -d "$CGROUP2_PATH" ]; then
        while IFS= read -r -d '' cgdir; do
            echo "bpftool cgroup detach $cgdir cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt"
            bpftool cgroup detach "$cgdir" cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt 2>/dev/null || echo "Failed to detach from $cgdir or not attached"
            #删除cgroup目录
            echo "rmdir $cgdir"
            rmdir "$cgdir" 2>/dev/null || echo "Failed to remove cgroup directory $cgdir or not empty"
        done < <(find "$CGROUP2_PATH" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null)
    else
        echo "CGROUP2_PATH does not exist: $CGROUP2_PATH"
    fi

    # Unpin BPF program
    echo "rm -f /sys/fs/bpf/xm_hbm_edt"
    rm -f /sys/fs/bpf/xm_hbm_edt 2>/dev/null || echo "BPF program not pinned or failed to unpin"

    # 提示，请后序手工删除该目录
    echo "!!! Please manually remove cgroup directory: $CGROUP2_PATH !!!"

    echo "HBM EDT cleanup completed"
}

update() {
    local cgroup_name=$1
    local rate_mbps=$2
    local verbose=$3
    local cgroup_id
    local cgroup_full_path

    cgroup_full_path=$(make_cgroup_path "$cgroup_name")
    if [ $? -ne 0 ] || [ -z "$cgroup_full_path" ]; then
        echo "load_bpf: failed to get/create cgroup path for $cgroup_name"
        return 1
    fi

    echo "Updating HBM EDT with cgroup: $cgroup_name, rate: ${rate_mbps}Mbps"

    if [ ! -d "$cgroup_full_path" ]; then
        echo "Cgroup path does not exist: $cgroup_full_path"
        exit 1
    fi

    # stat -Lc %i $cgroup_name 获取 cgroup id
    cgroup_id=$(stat -Lc %i "$cgroup_full_path")
    echo "Cgroup ID for $cgroup_full_path is $cgroup_id"

    #更新map中的速率值
    if ! update_rate "$cgroup_id" "$rate_mbps" "$verbose"; then
        echo "Failed to update rate in BPF map"
        exit 1
    fi

    echo "Rate updated successfully"
}

dump() {
    # Detach BPF programs from all cgroups under CGROUP2_PATH
    if [ -d "$CGROUP2_PATH" ]; then
        while IFS= read -r -d '' cgdir; do
            echo "bpftool cgroup list '$cgdir'"
            bpftool cgroup list "$cgdir" 2>/dev/null || echo "Failed to list cgroup $cgdir"
        done < <(find "$CGROUP2_PATH" -mindepth 1 -maxdepth 1 -type d -print0 2>/dev/null)
    else
        echo "CGROUP2_PATH does not exist: $CGROUP2_PATH"
    fi

    echo "Dumping HBM EDT statistics information"
    bpftool map dump name xm_hbm_edt_stat 2>/dev/null || echo "Failed to dump HBM EDT statistics information or map does not exist"
}

init_shell() {
    if [ $# -ne 1 ]; then
        echo "Usage: $0 init_shell <cgroup_name>"
        exit 1
    fi

    local cgroup_name=$1
    local cgroup_full_path shell_pid

    cgroup_full_path=$(make_cgroup_path "$cgroup_name")
    if [ $? -ne 0 ] || [ -z "$cgroup_full_path" ]; then
        echo "init_shell: failed to get/create cgroup path for $cgroup_name"
        return 1
    fi

    shell_pid=$(ps -o ppid= -p $$)
    shell_pid=$(echo $shell_pid | tr -d ' ')

    echo "Adding shell PID $shell_pid to cgroup: $cgroup_name"
    echo "$shell_pid" > "$cgroup_full_path/cgroup.procs"
    if [ $? -ne 0 ]; then
        echo "Failed to add PID to cgroup.procs"
        return 1
    fi

    if grep -q "^$shell_pid$" "$cgroup_full_path/cgroup.procs"; then
        echo "Successfully added shell PID $shell_pid to cgroup $cgroup_name"
        return 0
    else
        echo "Failed to verify PID in cgroup.procs"
        return 1
    fi
}

usage() {
    echo "Usage: $0 {init <cgroup_name> <rate_mbps> <ifname> <edt_bpf_path>|unload <cgroup_name>|clean|update <cgroup_name> <rate_mbps> <verbose>|dump}"
    echo "Commands:"
    echo "  init <cgroup_name> <rate_mbps> <ifname> <edt_bpf_path> - Initialize HBM EDT with specified cgroup, rate, interface, and BPF file"
    echo "  unload <cgroup_name>                                   - Detach BPF program and remove rate entry for specified cgroup"
    echo "  clean                                                  - Clean up HBM EDT configuration"
    echo "  update <cgroup_name> <rate_mbps> <verbose>            - Update HBM EDT rate for specified cgroup and verbosity"
    echo "  dump                                                   - Dump HBM EDT statistics information"
    echo "  init_shell <cgroup_name>                               - Add the current terminal shell PID to the procs of the specified cgroup."
}

case $COMMAND in
    init)
        if [ $# -ne 5 ]; then
            usage
            exit 1
        fi
        init "$2" "$3" "$4" "$5"
        ;;
    unload)
        if [ $# -ne 2 ]; then
            usage
            exit 1
        fi
        shift
        unload "$1"
        ;;
    clean)
        clean
        ;;
    update)
        if [ $# -ne 4 ]; then
            usage
            exit 1
        fi
        shift
        update "$@"
        ;;
    dump)
        dump
        ;;
    init_shell)
        if [ $# -ne 2 ]; then
            usage
            exit 1
        fi
        shift
        init_shell "$1"
        ;;
    *)
        usage
        exit 1
        ;;
esac