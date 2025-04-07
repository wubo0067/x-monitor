#!/usr/bin/env python3
# encoding: utf-8

"""
Author: CalmWu
Date: 2025--04-03
Description: A script to configure and manage XPS (Transmit Packet Steering) settings for network devices.

# 配置 eth0 的 XPS
./script.py eth0

# 从备份恢复 eth0 的 XPS 配置
./script.py eth0 --restore

# 从指定备份文件恢复
./script.py eth0 --restore --backup-file /path/to/backup.txt
"""

import os
import multiprocessing
import subprocess
import argparse
import sys
import re

def save_xps_config(dev):
    """保存网卡所有队列的 XPS 配置以便后续恢复"""
    backup_xps = {}
    txq_dir = f"/sys/class/net/{dev}/queues/"

    if not os.path.exists(txq_dir):
        print(f"Error: Device {dev} not found or no queue information available")
        return None

    for txq in os.listdir(txq_dir):
        if txq.startswith("tx-"):
            xps_path = f"{txq_dir}/{txq}/xps_cpus"
            try:
                with open(xps_path, "r") as fxps:
                    backup_xps[txq] = fxps.read().strip()
            except Exception as e:
                print(f"Warning: Failed to read XPS config for {txq}: {e}")

    # 将备份信息保存到文件
    backup_file_path = f"/tmp/{dev}_xps_backup.txt"
    try:
        with open(backup_file_path, "w") as backup_file:
            for txq, cpu_map in backup_xps.items():
                backup_file.write(f"{txq}: {cpu_map}\n")
        print(f"Backup XPS configuration saved to {backup_file_path}")
        return backup_xps
    except Exception as e:
        print(f"Error: Failed to save backup to file: {e}")
        return backup_xps  # 即使文件保存失败，也返回配置信息

def restore_xps_config(dev, backup_file=None):
    """从备份文件恢复 XPS 配置"""
    if backup_file is None:
        backup_file = f"/tmp/{dev}_xps_backup.txt"

    if not os.path.exists(backup_file):
        print(f"Error: Backup file {backup_file} not found")
        return False

    try:
        # 从备份文件读取配置
        configs = {}
        with open(backup_file, "r") as f:
            for line in f:
                if ":" in line:
                    queue, cpu_map = line.strip().split(":", 1)
                    configs[queue.strip()] = cpu_map.strip()

        # 应用配置
        success = True
        for queue, cpu_map in configs.items():
            xps_path = f"/sys/class/net/{dev}/queues/{queue}/xps_cpus"
            try:
                with open(xps_path, "w") as f:
                    f.write(cpu_map)
                print(f"Restored XPS config for {queue}: {cpu_map}")
            except Exception as e:
                print(f"Failed to restore XPS for {queue}: {e}")
                success = False

        return success
    except Exception as e:
        print(f"Error during XPS restoration: {e}")
        return False

def get_numa_node(dev):
    """获取设备的 NUMA 节点"""
    numa_path = f"/sys/class/net/{dev}/device/numa_node"
    try:
        if not os.path.exists(numa_path):
            # 尝试备用路径
            pci_path = None
            for path in [f"/sys/class/net/{dev}/device", f"/sys/class/net/{dev}"]:
                if os.path.exists(path) and os.path.islink(path):
                    pci_path = os.path.realpath(path)
                    break

            if pci_path:
                numa_paths = [f"{pci_path}/numa_node", f"{os.path.dirname(pci_path)}/numa_node"]
                for path in numa_paths:
                    if os.path.exists(path):
                        numa_path = path
                        break

        if not os.path.exists(numa_path):
            print(f"Warning: NUMA node path not found for {dev}, falling back to NUMA node 0")
            return 0

        with open(numa_path, "r") as fnuma:
            numa_node = int(fnuma.read().strip())
            if numa_node < 0:  # 某些系统可能返回 -1 表示无 NUMA 信息
                print(f"Warning: Invalid NUMA node ({numa_node}) for {dev}, falling back to NUMA node 0")
                numa_node = 0
        print(f"NUMA node for {dev}: {numa_node}")
        return numa_node
    except Exception as e:
        print(f"Error retrieving NUMA node for {dev}: {e}")
        print("Falling back to NUMA node 0")
        return 0

def get_numa_cpus(numa_node):
    """获取 NUMA 节点对应的 CPU 核心 ID 列表"""
    cpu_ids = []

    # 尝试方法 1: lscpu
    try:
        result = subprocess.check_output(f"lscpu | grep 'NUMA node{numa_node} CPU(s)'", shell=True).decode().strip()
        cpu_ranges = result.split(":")[1].strip()  # 提取核心范围部分，例如 "24-31, 88-95"

        # 解析范围并生成核心 ID 列表
        for cpu_range in cpu_ranges.split(","):
            cpu_range = cpu_range.strip()
            if "-" in cpu_range:  # 处理范围，例如 "24-31"
                start, end = map(int, cpu_range.split("-"))
                cpu_ids.extend(range(start, end + 1))
            else:  # 处理单个核心，例如 "88"
                cpu_ids.append(int(cpu_range))
    except Exception as e:
        print(f"lscpu method failed: {e}")

        # 尝试方法 2: numactl
        try:
            result = subprocess.check_output(f"numactl -H | grep 'node {numa_node} cpus:'", shell=True).decode().strip()
            # 格式例如："node 0 cpus: 0 1 2 3 4 5 6 7"
            cpu_list = result.split("cpus:")[1].strip().split()
            cpu_ids = [int(cpu) for cpu in cpu_list]
        except Exception as e2:
            print(f"numactl method failed: {e2}")

            # 尝试方法 3: 读取 sys 文件系统
            try:
                cpulist_path = f"/sys/devices/system/node/node{numa_node}/cpulist"
                if os.path.exists(cpulist_path):
                    with open(cpulist_path, "r") as f:
                        cpulist = f.read().strip()
                        # 解析格式如 "0-3,8-11"
                        for cpu_range in cpulist.split(","):
                            cpu_range = cpu_range.strip()
                            if "-" in cpu_range:
                                start, end = map(int, cpu_range.split("-"))
                                cpu_ids.extend(range(start, end + 1))
                            else:
                                cpu_ids.append(int(cpu_range))
            except Exception as e3:
                print(f"sysfs method failed: {e3}")

    if not cpu_ids:
        print(f"Warning: Failed to get CPU cores for NUMA node {numa_node}")
        # 获取所有 CPU 核心作为备选
        try:
            cpu_count = multiprocessing.cpu_count()
            print(f"Falling back to using all available CPUs (0-{cpu_count-1})")
            cpu_ids = list(range(cpu_count))
        except:
            print("Error: Could not determine CPU count, using default range 0-7")
            cpu_ids = list(range(8))  # 默认范围

    print(f"CPU cores for NUMA node {numa_node}: {cpu_ids}")
    return cpu_ids

# core-num 转换为 16 进制 printf %x $((1 << <core_number> ))
def hex_bitmap_from_cpus(cpu_list, total_cpus):
    """从 CPU 列表生成十六进制位图字符串"""
    # 计算需要多少个 32 位整数
    bitmap_count = (total_cpus + 31) // 32
    bitmap = [0] * bitmap_count

    # 设置位图
    for cpu in cpu_list:
        if 0 <= cpu < total_cpus:
            bitmap[cpu // 32] |= (1 << (cpu % 32))

    # 转换为十六进制字符串并反转（低位在前）
    return ','.join([f"{value:08x}" for value in bitmap])

def setup_xps(dev, numa_cpus):
    """设置 XPS 分配逻辑，在队列间均匀分配 NUMA 核心"""
    # 获取系统总核心数量
    total_cpu_count = multiprocessing.cpu_count()

    txq_dir = f"/sys/class/net/{dev}/queues/"
    tx_queues = sorted([q for q in os.listdir(txq_dir) if q.startswith("tx-")])
    queue_count = len(tx_queues)

    if queue_count <= 0:
        print(f"Error: No TX queues found for {dev}")
        return False

    print(f"Found {queue_count} TX queues for {dev}")

    # 如果 NUMA 核心数量小于队列数，则部分队列需要共享核心
    if len(numa_cpus) < queue_count:
        print(f"Warning: More queues ({queue_count}) than CPU cores ({len(numa_cpus)})")
        # 复制 NUMA 核心列表直到数量超过队列数
        extended_cores = numa_cpus * ((queue_count // len(numa_cpus)) + 1)
        numa_cpus = extended_cores[:queue_count]

    # 分配策略：尽量均匀分配核心到队列
    cpu_per_queue = max(1, len(numa_cpus) // queue_count)
    extra_cpus = len(numa_cpus) % queue_count

    success = True
    cpu_idx = 0

    for i, queue in enumerate(tx_queues):
        # 决定这个队列分配多少个 CPU
        cores_for_queue = cpu_per_queue + (1 if i < extra_cpus else 0)

        # 选择用于此队列的 CPU 核心
        queue_cpus = []
        for _ in range(cores_for_queue):
            queue_cpus.append(numa_cpus[cpu_idx])
            cpu_idx = (cpu_idx + 1) % len(numa_cpus)

        # 生成位图
        bitmap = hex_bitmap_from_cpus(queue_cpus, total_cpu_count)

        # 写入 XPS 配置
        xps_path = f"{txq_dir}/{queue}/xps_cpus"
        try:
            with open(xps_path, "w") as fxps:
                fxps.write(bitmap)
            print(f"Set XPS for {queue} with bitmap: {bitmap} (CPUs: {queue_cpus})")
        except Exception as e:
            print(f"Error setting XPS for {queue}: {e}")
            success = False

    return success

def process_devs(dev, restore=False, backup_file=None):
    """主处理函数"""
    if restore:
        return restore_xps_config(dev, backup_file)

    # 1. 备份当前 XPS 配置
    backup_data = save_xps_config(dev)
    if backup_data is None:
        print(f"Failed to process device {dev}")
        return False

    # 2. 获取 NUMA 节点
    numa_node = get_numa_node(dev)

    # 3. 获取 NUMA 节点对应的 CPU 核心
    cores = get_numa_cpus(numa_node)
    print(f"Using NUMA node {numa_node} cores: {cores}")

    # 4. 设置 XPS 分配
    return setup_xps(dev, cores)

def main():
    parser = argparse.ArgumentParser(description='XPS (Transmit Packet Steering) Configuration Tool')
    parser.add_argument('device', help='Network device name (e.g. eth0)')
    parser.add_argument('-r', '--restore', action='store_true', help='Restore XPS configuration from backup')
    parser.add_argument('-b', '--backup-file', help='Specify backup file path (for restore)')

    args = parser.parse_args()

    result = process_devs(args.device, args.restore, args.backup_file)

    if result:
        print(f"{'Restoration' if args.restore else 'Configuration'} completed successfully")
    else:
        print(f"{'Restoration' if args.restore else 'Configuration'} completed with errors")
        sys.exit(1)

if __name__ == '__main__':
    main()