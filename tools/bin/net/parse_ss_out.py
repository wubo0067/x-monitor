#!/usr/bin/env python3
import subprocess
import sys
import re

def parse_ss_output(sport):
    # 获取命令输出
    cmd = ["ss", "-tinm", "state", "established", f"sport = :{sport}"]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, text=True)

    # 分行处理
    lines = result.stdout.strip().splitlines()
    i = 0
    total = len(lines)

    while i < total:
        line = lines[i].strip()
        if not line or line.startswith("Recv-Q"):
            i += 1
            continue

        # 匹配连接信息行
        conn_match = re.search(r'(\d+\.\d+\.\d+\.\d+:\d+)\s+(\d+\.\d+\.\d+\.\d+:\d+)', line)
        if conn_match:
            local = conn_match.group(1)
            peer = conn_match.group(2)
            print("=" * 40)
            print(f"🟢 本地地址：{local}")
            print(f"🔵 对端地址：{peer}")

            # 查看下一行是否包含 skmem 和 TCP 状态信息（或当前行）
            i += 1
            if i < total and 'skmem:' in lines[i]:
                sk_line = lines[i].strip()

                # --- 解析 skmem ---
                skmem_match = re.search(r'skmem:\((.*?)\)', sk_line)
                if skmem_match:
                    sk_fields = skmem_match.group(1).split(',')
                    skmem_map = {
                        'r': '接收队列使用内存',
                        'rb': '接收缓冲区大小',
                        't': '发送队列使用内存',
                        'tb': '发送缓冲区大小',
                        'f': '前向分配内存',
                        'w': '写队列排队数据',
                        'o': '选项内存',
                        'bl': 'backlog 队列内存',
                        'd': '丢包次数'
                    }
                    print("📦 Socket 内存信息（单位：字节）：")
                    for field in sk_fields:
                        key_match = re.match(r'[a-z]+', field)
                        val_match = re.search(r'\d+', field)
                        if key_match and val_match:
                            key = key_match.group()
                            value = val_match.group()
                            desc = skmem_map.get(key, key)
                            print(f"  {desc}: {value}")

                # --- 解析 TCP 状态字段 ---
                details = sk_line  # 当前行可能含 TCP 信息
                tcp_keys = ["rtt:", "bytes_sent:", "cwnd:", "send", "delivery_rate", "minrtt"]
                fields = {
                    "rtt": "往返时延 RTT（毫秒）",
                    "ato": "ACK 超时时间（毫秒）",
                    "mss": "最大报文段长度 MSS（字节）",
                    "pmtu": "路径 MTU（字节）",
                    "rcvmss": "对端 MSS",
                    "advmss": "本端 MSS",
                    "cwnd": "拥塞窗口大小",
                    "bytes_sent": "已发送字节数（Bytes）",
                    "bytes_acked": "已确认字节数",
                    "bytes_received": "已接收字节数",
                    "segs_out": "发送段数",
                    "segs_in": "接收段数",
                    "data_segs_out": "数据发送段数",
                    "data_segs_in": "数据接收段数",
                    "send": "当前发送速率（bps）",
                    "lastsnd": "上次发送时间（ms）",
                    "lastrcv": "上次接收时间（ms）",
                    "lastack": "上次 ACK 时间（ms）",
                    "pacing_rate": "Pacing 速率（bps）",
                    "delivery_rate": "交付速率（bps）",
                    "delivered": "累计已交付段数",
                    "app_limited": "应用层限速",
                    "busy": "发送端忙碌时长（ms）",
                    "unacked": "未确认段数",
                    "rcv_space": "接收窗口空间",
                    "rcv_ssthresh": "接收慢启动门限",
                    "minrtt": "最小 RTT（毫秒）"
                }

                if any(k in details for k in tcp_keys):
                    print("📊 TCP 状态信息：")
                    for key, desc in fields.items():
                        match = re.search(rf"{key}:(\S+)", details)
                        if match:
                            value = match.group(1)
                            value_clean = re.sub(r'[a-zA-Z]+$', '', value)
                            try:
                                if key == "rtt":
                                    main_rtt = value.split('/')[0]
                                    print(f"  {desc}: {main_rtt} ms")
                                elif key in ["send", "pacing_rate", "delivery_rate"]:
                                    print(f"  {desc}: {int(float(value_clean)):,} bps")
                                elif key in ["busy", "lastsnd", "lastrcv", "lastack", "minrtt"]:
                                    print(f"  {desc}: {float(value_clean)} ms")
                                elif key in ["cwnd", "rcv_space", "rcv_ssthresh", "delivered", "unacked"]:
                                    print(f"  {desc}: {int(value_clean)}")
                                elif key == "app_limited":
                                    print(f"  {desc}: 是" if value else "否")
                                else:
                                    print(f"  {desc}: {value}")
                            except ValueError:
                                print(f"  {desc}: 解析失败（原值：{value}）")
                else:
                    print("📊 TCP 状态信息：⚠️ 未找到")
        else:
            i += 1

        i += 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("用法：python3 parse_ss_out.py <sport>")
        sys.exit(1)

    sport = sys.argv[1]
    parse_ss_output(sport)
