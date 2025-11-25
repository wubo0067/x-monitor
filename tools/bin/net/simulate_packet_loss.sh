#!/bin/bash

usage() {
    echo "用法:"
    echo "  $0 netem <nic> <loss_rate>                # 使用netem模块模拟丢包"
    echo "  $0 netem_restore <nic>                    # 恢复网卡正常状态"
    echo "  $0 netem_state <nic>                      # 显示网卡当前状态"
    echo "  $0 iptables_drop --dst=ip:port            # 添加目标iptables DROP规则"
    echo "  $0 iptables_drop --src=ip:port            # 添加源iptables DROP规则"
    echo "  $0 iptables_restore --dst=ip:port         # 删除目标iptables DROP规则"
    echo "  $0 iptables_restore --src=ip:port         # 删除源iptables DROP规则"
    echo "  $0 tc_icmp_drop <nic>                     # 使用tc过滤并丢弃ICMP数据包"
    echo "  $0 tc_icmp_restore <nic>                  # 恢复ICMP数据包处理"
    echo "  $0 xdp_icmp_drop <nic>                    # 使用XDP eBPF程序丢弃ICMP数据包"
    echo "  $0 xdp_icmp_restore <nic>                 # 恢复网卡XDP状态"
    echo ""
    echo "参数说明:"
    echo "  nic          网络接口名称 (如: eth0, ens33)"
    echo "  loss_rate    丢包率百分比 (如: 10 表示10%)"
    echo "  ip:port      IP地址和端口号 (端口可选，如: 192.168.1.1:80 或 192.168.1.1)"
    echo "  --dst=ip:port  目标地址和端口"
    echo "  --src=ip:port  源地址和端口"
}

CMD=$1
NIC=$2
LOSS=$3
IPTABLES_PARAM=$2

check_netem() {
    if ! lsmod | grep -v grep | grep -q sch_netem; then
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

check_clang() {
    if ! command -v clang >/dev/null 2>&1; then
        echo "错误: 未找到 clang 编译器，请先安装 clang"
        exit 1
    fi
}

check_bpftool() {
    if ! command -v bpftool >/dev/null 2>&1; then
        echo "错误: 未找到 bpftool 工具，请先安装 bpftool"
        exit 1
    fi
}

parse_ip_port() {
    local param="$1"
    local ip_port=""

    if [[ $param == --dst=* ]]; then
        ip_port="${param#--dst=}"
    elif [[ $param == --src=* ]]; then
        ip_port="${param#--src=}"
    else
        echo "参数格式错误: $param (应为 --dst=ip:port 或 --src=ip:port)"
        exit 1
    fi

    echo "$ip_port"
}

add_iptables_drop_rule() {
    local direction="$1"
    local ip_port="$2"
    local ip=""
    local port=""

    # 分离IP和端口
    if [[ $ip_port == *:* ]]; then
        ip="${ip_port%:*}"
        port="${ip_port##*:}"
    else
        ip="$ip_port"
    fi

    # 添加iptables规则 (使用-I在第一条位置插入)
    if [ "$direction" == "dst" ]; then
        if [ -n "$port" ]; then
            sudo iptables -I OUTPUT 1 -d "$ip" -p tcp --dport "$port" -j DROP
            sudo iptables -I FORWARD 1 -d "$ip" -p tcp --dport "$port" -j DROP
        else
            sudo iptables -I OUTPUT 1 -d "$ip" -j DROP
            sudo iptables -I FORWARD 1 -d "$ip" -j DROP
        fi
    elif [ "$direction" == "src" ]; then
        if [ -n "$port" ]; then
            sudo iptables -I INPUT 1 -s "$ip" -p tcp --sport "$port" -j DROP
            sudo iptables -I FORWARD 1 -s "$ip" -p tcp --sport "$port" -j DROP
        else
            sudo iptables -I INPUT 1 -s "$ip" -j DROP
            sudo iptables -I FORWARD 1 -s "$ip" -j DROP
        fi
    fi

    echo "已添加iptables DROP规则: $direction $ip_port"
}

remove_iptables_drop_rule() {
    local direction="$1"
    local ip_port="$2"
    local ip=""
    local port=""

    # 分离IP和端口
    if [[ $ip_port == *:* ]]; then
        ip="${ip_port%:*}"
        port="${ip_port##*:}"
    else
        ip="$ip_port"
    fi

    # 删除iptables规则
    if [ "$direction" == "dst" ]; then
        if [ -n "$port" ]; then
            sudo iptables -D OUTPUT -d "$ip" -p tcp --dport "$port" -j DROP 2>/dev/null
            sudo iptables -D FORWARD -d "$ip" -p tcp --dport "$port" -j DROP 2>/dev/null
        else
            sudo iptables -D OUTPUT -d "$ip" -j DROP 2>/dev/null
            sudo iptables -D FORWARD -d "$ip" -j DROP 2>/dev/null
        fi
    elif [ "$direction" == "src" ]; then
        if [ -n "$port" ]; then
            sudo iptables -D INPUT -s "$ip" -p tcp --sport "$port" -j DROP 2>/dev/null
            sudo iptables -D FORWARD -s "$ip" -p tcp --sport "$port" -j DROP 2>/dev/null
        else
            sudo iptables -D INPUT -s "$ip" -j DROP 2>/dev/null
            sudo iptables -D FORWARD -s "$ip" -j DROP 2>/dev/null
        fi
    fi

    echo "已删除iptables DROP规则: $direction $ip_port"
}

add_tc_icmp_drop_rule() {
    local nic="$1"

    # 添加clsact队列规则
    sudo tc qdisc add dev "$nic" clsact 2>/dev/null || true

    # 添加过滤器以丢弃ICMP数据包 (协议号1)
    sudo tc filter add dev "$nic" egress protocol ip \
       u32 match ip protocol 1 0xff \
       action drop

    echo "已在网卡 $nic 上设置ICMP数据包丢弃规则"
}

remove_tc_icmp_drop_rule() {
    local nic="$1"

    # 删除clsact队列规则会移除所有相关过滤器
    sudo tc qdisc del dev "$nic" clsact 2>/dev/null || true

    echo "已恢复网卡 $nic 上的ICMP数据包处理"
}

add_xdp_icmp_drop_rule() {
    local nic="$1"

    # 检查clang是否安装
    check_clang
    check_bpftool

    # 生成vmlinux.h文件
    if ! sudo bpftool btf dump file /sys/kernel/btf/vmlinux format c > /tmp/vmlinux.h; then
        echo "错误: 无法生成 vmlinux.h 文件"
        exit 1
    fi

    # 创建eBPF程序
    cat <<'EOF' > /tmp/drop_icmp.c
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#ifndef ETH_P_IP
#define ETH_P_IP 0x0800 /* Internet Protocol packet */
#endif

SEC("xdp")
int xdp_drop_icmp(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) return XDP_PASS;
    if (eth->h_proto != bpf_htons(ETH_P_IP)) return XDP_PASS;
    struct iphdr *iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end) return XDP_PASS;
    if (iph->protocol == IPPROTO_ICMP) return XDP_DROP;
    return XDP_PASS;
}
char __license[] SEC("license") = "GPL";
EOF

    # 编译eBPF程序
    if ! clang -O2 -target bpf -I/tmp -c /tmp/drop_icmp.c -o /tmp/drop_icmp.o; then
        echo "错误: eBPF程序编译失败"
        rm -f /tmp/drop_icmp.c /tmp/drop_icmp.o /tmp/vmlinux.h
        exit 1
    fi

    # 挂载eBPF程序到网卡
    if ! sudo ip link set "$nic" xdp obj /tmp/drop_icmp.o sec xdp; then
        echo "错误: 无法将eBPF程序挂载到网卡 $nic"
        rm -f /tmp/drop_icmp.c /tmp/drop_icmp.o /tmp/vmlinux.h
        exit 1
    fi

    # 保存文件路径以便后续清理
    echo "/tmp/drop_icmp.c /tmp/drop_icmp.o /tmp/vmlinux.h" > "/tmp/xdp_icmp_${nic}.tmp"

    echo "已在网卡 $nic 上使用XDP丢弃ICMP数据包"
}

remove_xdp_icmp_drop_rule() {
    local nic="$1"

    # 卸载网卡上的XDP程序
    sudo ip link set "$nic" xdp off 2>/dev/null

    # 清理临时文件
    if [ -f "/tmp/xdp_icmp_${nic}.tmp" ]; then
        files=$(cat "/tmp/xdp_icmp_${nic}.tmp")
        rm -f $files
        rm -f "/tmp/xdp_icmp_${nic}.tmp"
    fi

    echo "已恢复网卡 $nic 上的XDP处理"
}

if [ "$CMD" == "netem" ]; then
    if [ -z "$NIC" ] || [ -z "$LOSS" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi
    check_netem
    sudo tc qdisc del dev $NIC root 2>/dev/null
    sudo tc qdisc add dev $NIC root netem loss ${LOSS}%
    echo "已在网卡 $NIC 上设置 ${LOSS}% 丢包率"

elif [ "$CMD" == "netem_restore" ]; then
    if [ -z "$NIC" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi
    sudo tc qdisc del dev $NIC root 2>/dev/null
    echo "已恢复网卡 $NIC 的正常网络状态"

elif [ "$CMD" == "netem_state" ]; then
    if [ -z "$NIC" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi
    sudo tc -s qdisc show dev $NIC

elif [ "$CMD" == "iptables_drop" ]; then
    if [ -z "$IPTABLES_PARAM" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi

    if [[ $IPTABLES_PARAM == --dst=* ]]; then
        ip_port=$(parse_ip_port "$IPTABLES_PARAM")
        add_iptables_drop_rule "dst" "$ip_port"
    elif [[ $IPTABLES_PARAM == --src=* ]]; then
        ip_port=$(parse_ip_port "$IPTABLES_PARAM")
        add_iptables_drop_rule "src" "$ip_port"
    else
        echo "参数格式错误: $IPTABLES_PARAM (应为 --dst=ip:port 或 --src=ip:port)"
        usage
        exit 1
    fi

elif [ "$CMD" == "iptables_restore" ]; then
    if [ -z "$IPTABLES_PARAM" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi

    if [[ $IPTABLES_PARAM == --dst=* ]]; then
        ip_port=$(parse_ip_port "$IPTABLES_PARAM")
        remove_iptables_drop_rule "dst" "$ip_port"
    elif [[ $IPTABLES_PARAM == --src=* ]]; then
        ip_port=$(parse_ip_port "$IPTABLES_PARAM")
        remove_iptables_drop_rule "src" "$ip_port"
    else
        echo "参数格式错误: $IPTABLES_PARAM (应为 --dst=ip:port 或 --src=ip:port)"
        usage
        exit 1
    fi

elif [ "$CMD" == "tc_icmp_drop" ]; then
    if [ -z "$NIC" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi

    add_tc_icmp_drop_rule "$NIC"

elif [ "$CMD" == "tc_icmp_restore" ]; then
    if [ -z "$NIC" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi

    remove_tc_icmp_drop_rule "$NIC"

elif [ "$CMD" == "xdp_icmp_drop" ]; then
    if [ -z "$NIC" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi

    add_xdp_icmp_drop_rule "$NIC"

elif [ "$CMD" == "xdp_icmp_restore" ]; then
    if [ -z "$NIC" ]; then
        echo "错误: 缺少必要参数"
        usage
        exit 1
    fi

    remove_xdp_icmp_drop_rule "$NIC"

elif [ "$CMD" == "usage" ] || [ "$CMD" == "help" ] || [ "$CMD" == "-h" ] || [ "$CMD" == "--help" ]; then
    usage
    exit 0

else
    echo "未知命令: $CMD"
    usage
    exit 1
fi