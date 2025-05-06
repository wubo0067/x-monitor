#!/usr/bin/env python3
import subprocess
import sys
import re

# watch -n 1 "ss -itnnm | grep -A 10 <port>"

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

        conn_match = re.search(r'(\d+\.\d+\.\d+\.\d+:\d+)\s+(\d+\.\d+\.\d+\.\d+:\d+)', line)
        if conn_match:
            local = conn_match.group(1)
            peer = conn_match.group(2)
            print("=" * 40)
            print(f"🟢 Local Address: {local}")
            print(f"🔵 Peer Address: {peer}")

            # 查看下一行是否包含 skmem 和 TCP 状态信息（或当前行）
            i += 1
            if i < total and 'skmem:' in lines[i]:
                sk_line = lines[i].strip()

                # --- 解析 skmem ---
                skmem_match = re.search(r'skmem:\((.*?)\)', sk_line)
                if skmem_match:
                    sk_fields = skmem_match.group(1).split(',')
                    skmem_map = {
                        # 当前已分配用于接收的数据量（单位：字节）。表示当前正在占用的接收内存大小，包括未被应用读取的数据。
                        'r': 'Receive Queue Memory',
                        # 该套接字的接收缓冲区总大小上限（字节），由 sysctl net.ipv4.tcp_rmem 或 SO_RCVBUF 套接字选项决定。
                        'rb': 'Receive Buffer Size',
                        # 当前分配给该套接字发送缓冲区的内存大小（字节）。从 SO_SNDBUF 配置的缓冲区中实际使用的部分。
                        't': 'Send Queue Memory',
                        # 该套接字的发送缓冲区总大小上限（字节）。由 sysctl net.ipv4.tcp_wmem 或 SO_SNDBUF 套接字选项决定。
                        'tb': 'Send Buffer Size',
                        'f': 'Forward Allocation Memory',
                        # 发送队列中等待传输的数据量（字节）。持续高位，可能因网络拥塞或对端接收慢导致积压
                        'w': 'Write Queue Pending Data',
                        'o': 'Options Memory',
                        'bl': 'Backlog Queue Memory',
                        # 因缓冲区不足导致的数据包丢弃次数。
                        # 接收方向：r<val1> 达到 rb<val2> 时丢弃新数据。
                        # 发送方向：t<val3> 达到 tb<val4> 时丢弃应用写入的数据。
                        'd': 'Drop Count'
                    }
                    print("📦 Socket Memory Info (bytes):")
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
                    # tc qdisc drop 包后，rtt 会上升。
                    "rtt": "Round Trip Time (ms)",
                    "ato": "ACK Timeout (ms)",
                    "mss": "Max Segment Size (bytes)",
                    "pmtu": "Path MTU (bytes)",
                    "rcvmss": "Receive MSS",
                    "advmss": "Advertised MSS",
                    # Congestion Window 来控制发送速率，出现丢包时降低 cwnd。Cubic 会慎重降窗
                    # 表示在网络中 in-flight 的字节数最多为 cwnd 个 MSS 单位。
                    "cwnd": "Congestion Window",
                    # 慢启动阈值，当 cwnd > ssthresh 时退出慢启动阶段进入拥塞避免阶段
                    "ssthresh": "Slow Start Threshold",
                    "bytes_sent": "Bytes Sent",
                    "bytes_acked": "Bytes Acknowledged",
                    "bytes_received": "Bytes Received",
                    "segs_out": "Segments Out",
                    "segs_in": "Segments In",
                    "data_segs_out": "Data Segments Out",
                    "data_segs_in": "Data Segments In",
                    "send": "Send Rate (bps)",
                    "lastsnd": "Last Send Time (ms)",
                    "lastrcv": "Last Receive Time (ms)",
                    "lastack": "Last ACK Time (ms)",
                    "pacing_rate": "Pacing Rate (bps)",
                    "delivery_rate": "Delivery Rate (bps)",
                    "delivered": "Delivered Segments",
                    # 如果 app_limited 出现，意味着 TCP 协议栈准备好发送更多的数据（根据拥塞窗口和接收窗口），
                    # 但是应用程序没有足够的数据提供给 TCP 协议栈发送。
                    "app_limited": "Application Limited",
                    "busy": "Busy Time (ms)",
                    "unacked": "Unacknowledged Segments",
                    "rcv_space": "Receive Window Space",
                    "rcv_ssthresh": "Receive Slow Start Threshold",
                    "minrtt": "Minimum RTT (ms)"
                }

                if any(k in details for k in tcp_keys):
                    print("📊 TCP Status Information:")
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
                                    print(f"  {desc}: Yes" if value else "No")
                                else:
                                    print(f"  {desc}: {value}")
                            except ValueError:
                                print(f"  {desc}: Parse failed (raw: {value})")
                    # 检查是否存在 ssthresh，如果不存在则显示为 0
                    if not re.search(r"ssthresh:", details):
                        print(f"  Slow Start Threshold: 0")
                else:
                    print("📊 TCP Status Information: ⚠️ Not Found")
        else:
            i += 1

        i += 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 parse_ss_out.py <sport>")
        sys.exit(1)

    sport = sys.argv[1]
    parse_ss_output(sport)