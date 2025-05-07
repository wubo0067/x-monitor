#!/usr/bin/env python3
import subprocess
import sys
import re
import time
import argparse
import signal
import datetime

# 处理 Ctrl+C 信号
running = True
def signal_handler(sig, frame):
    global running
    print("\nReceived Ctrl+C, stopping data collection...")
    running = False
signal.signal(signal.SIGINT, signal_handler)

def parse_ss_output(sport, output_file=None):
    # 获取命令输出
    cmd = ["ss", "-tinm", "state", "established", f"sport = :{sport}"]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, text=True)

    # 获取时间戳
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # 准备输出内容
    output_buffer = []
    output_buffer.append(f"==== Collection Time: {timestamp} ====")

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
            output_buffer.append("=" * 40)
            output_buffer.append(f"🟢 Local Address: {local}")
            output_buffer.append(f"🔵 Peer Address: {peer}")

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
                        'w': 'Write Queue Pending Data',
                        'o': 'Options Memory',
                        'bl': 'Backlog Queue Memory',
                        # 因缓冲区不足导致的数据包丢弃次数。
                        # 接收方向：r<val1> 达到 rb<val2> 时丢弃新数据。
                        # 发送方向：t<val3> 达到 tb<val4> 时丢弃应用写入的数据。
                        'd': 'Drop Count'
                    }
                    output_buffer.append("📦 Socket Memory Info (bytes):")
                    for field in sk_fields:
                        key_match = re.match(r'[a-z]+', field)
                        val_match = re.search(r'\d+', field)
                        if key_match and val_match:
                            key = key_match.group()
                            value = val_match.group()
                            desc = skmem_map.get(key, key)
                            output_buffer.append(f"  {desc}: {value}")

                # --- 解析 TCP 状态字段 ---
                details = sk_line  # 当前行可能含 TCP 信息
                tcp_keys = ["rtt:", "bytes_sent:", "cwnd:", "send", "delivery_rate", "minrtt"]
                fields = {
                    "rtt": "Round Trip Time (ms)",
                    "ato": "ACK Timeout (ms)",
                    "mss": "Max Segment Size (bytes)",
                    "pmtu": "Path MTU (bytes)",
                    "rcvmss": "Receive MSS",
                    "advmss": "Advertised MSS",
                    # 当 cwnd (10) < ssthresh (16) 时，意味着
                    #   连接处于积极增长阶段：TCP 算法认为网络仍有可用容量
                    # Congestion Window 来控制发送速率，出现丢包时降低 cwnd。
                    # 发送端最多可以发送 cwnd 个 MSS 的数据而不等待确认
                    # 一旦 cwnd 超过 ssthresh (16)，连接将转入拥塞避免阶段，增长速率变为线性
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
                    output_buffer.append("📊 TCP Status Information:")
                    for key, desc in fields.items():
                        match = re.search(rf"{key}:(\S+)", details)
                        if match:
                            value = match.group(1)
                            value_clean = re.sub(r'[a-zA-Z]+$', '', value)
                            try:
                                if key == "rtt":
                                    main_rtt = value.split('/')[0]
                                    output_buffer.append(f"  {desc}: {main_rtt} ms")
                                elif key in ["send", "pacing_rate", "delivery_rate"]:
                                    output_buffer.append(f"  {desc}: {int(float(value_clean)):,} bps")
                                elif key in ["busy", "lastsnd", "lastrcv", "lastack", "minrtt"]:
                                    output_buffer.append(f"  {desc}: {float(value_clean)} ms")
                                elif key in ["cwnd", "rcv_space", "rcv_ssthresh", "delivered", "unacked", "ssthresh"]:
                                    output_buffer.append(f"  {desc}: {int(value_clean)}")
                                elif key == "app_limited":
                                    output_buffer.append(f"  {desc}: Yes" if value else "No")
                                else:
                                    output_buffer.append(f"  {desc}: {value}")
                            except ValueError:
                                output_buffer.append(f"  {desc}: Parse failed (raw: {value})")
                    # 检查是否存在 ssthresh，如果不存在则显示为 0
                    if not re.search(r"ssthresh:", details):
                        output_buffer.append(f"  Slow Start Threshold: 0")
                else:
                    output_buffer.append("📊 TCP Status Information: ⚠️ Not Found")
        else:
            i += 1

        i += 1

    # 输出到控制台
    for line in output_buffer:
        print(line)

    # 如果指定了输出文件，则写入文件
    if output_file:
        with open(output_file, 'a', encoding='utf-8') as f:
            for line in output_buffer:
                f.write(line + '\n')
            f.write('\n')  # 每次采集后增加一个空行，便于区分


def main():
    # 解析命令行参数
    parser = argparse.ArgumentParser(description='Parse and display TCP socket information')
    parser.add_argument('sport', type=str, help='Source port to monitor')
    parser.add_argument('-i', '--interval', type=float, default=1.0,
                        help='Collection interval in seconds (default: 1.0)')
    parser.add_argument('-o', '--output', type=str, help='Output file path')
    args = parser.parse_args()

    print(f"Monitoring TCP connections on port {args.sport}")
    print(f"Collection interval: {args.interval} seconds")
    print(f"Output file: {args.output if args.output else 'None (console only)'}")
    print("Press Ctrl+C to stop monitoring")
    print()

    # 循环采集数据，直到 Ctrl+C 中断
    try:
        while running:
            parse_ss_output(args.sport, args.output)
            time.sleep(args.interval)
    except Exception as e:
        print(f"Error occurred: {e}")

    print("Data collection stopped.")


if __name__ == "__main__":
    main()