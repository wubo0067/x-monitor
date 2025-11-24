#!/bin/bash
# 用法:
#   ./netem_loss.sh loss <nic> <loss_rate>
#   ./netem_loss.sh restore <nic>
#   ./netem_loss.sh state <nic>

CMD=$1
NIC=$2
LOSS=$3

check_netem() {
    if ! lsmod | grep -q sch_netem; then
        echo "sch_netem 模块未加载，尝试安装并加载..."
        # 针对 RHEL/CentOS 系列，安装 kernel-modules-extra
        if command -v dnf >/dev/null 2>&1; then
            sudo dnf install -y kernel-modules-extra
        elif command -v yum >/dev/null 2>&1; then
            sudo yum install -y kernel-modules-extra
        fi
        # 加载模块
        sudo modprobe sch_netem
    fi
}

if [ "$CMD" == "loss" ]; then
    if [ -z "$NIC" ] || [ -z "$LOSS" ]; then
        echo "用法: $0 loss <nic> <loss_rate>"
        exit 1
    fi
    check_netem
    sudo tc qdisc del dev $NIC root 2>/dev/null
    sudo tc qdisc add dev $NIC root netem loss ${LOSS}%
    echo "已在网卡 $NIC 上设置 ${LOSS}% 丢包率"

elif [ "$CMD" == "restore" ]; then
    if [ -z "$NIC" ]; then
        echo "用法: $0 restore <nic>"
        exit 1
    fi
    sudo tc qdisc del dev $NIC root 2>/dev/null
    echo "已恢复网卡 $NIC 的正常网络状态"

elif [ "$CMD" == "state" ]; then
    if [ -z "$NIC" ]; then
        echo "用法: $0 state <nic>"
        exit 1
    fi
    sudo tc -s qdisc show dev $NIC

else
    echo "未知命令: $CMD"
    echo "用法: $0 {loss|restore|state} ..."
    exit 1
fi
