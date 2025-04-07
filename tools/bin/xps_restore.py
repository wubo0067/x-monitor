#!/usr/bin/env python
# encoding: utf-8
import os

def restore_xps_config(dev):
    # 备份文件路径
    backup_file = f"/tmp/{dev}_xps_backup.txt"

    # 检查备份文件是否存在
    if not os.path.exists(backup_file):
        print(f"Backup file not found: {backup_file}")
        return

    print(f"Restoring XPS configuration for {dev} from {backup_file}")

    # 读取备份文件内容
    with open(backup_file, "r") as fbackup:
        for line in fbackup:
            # 解析队列名称和对应的位图值
            txq, cpu_map = line.strip().split(":")
            txq = txq.strip()  # 队列名称
            cpu_map = cpu_map.strip()  # 位图值

            # 构造队列的 xps_cpus 路径
            txq_path = f"/sys/class/net/{dev}/queues/{txq}/xps_cpus"

            # 将备份位图值写入对应的队列
            try:
                with open(txq_path, "w") as fxps:
                    fxps.write(cpu_map)
                print(f"Restored XPS for {txq} with bitmap: {cpu_map}")
            except Exception as e:
                print(f"Error restoring XPS for {txq}: {e}")

if __name__ == "__main__":
    # 替换为目标网卡名，例如 "eth0"
    device_name = input("Enter the device name: ")
    restore_xps_config(device_name)
