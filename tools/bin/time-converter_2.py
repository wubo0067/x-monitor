#!/usr/bin/env python3
import time
import datetime

def get_boot_time():
    """获取系统启动时间"""
    with open('/proc/uptime', 'r') as f:
        uptime_seconds = float(f.readline().split()[0])
    return time.time() - uptime_seconds

def wall_to_uptime(wall_time_str):
    """
    将墙上时间转换为系统启动后的秒数
    
    参数:
    wall_time_str: 字符串格式的时间，如 "2024-01-03 14:30:25"
    
    返回:
    从系统启动到指定时间的秒数
    """
    wall_time = datetime.datetime.strptime(wall_time_str, "%Y-%m-%d %H:%M:%S")
    wall_timestamp = time.mktime(wall_time.timetuple())
    boot_time = get_boot_time()
    uptime = wall_timestamp - boot_time
    return uptime

def uptime_to_wall(uptime_seconds):
    """
    将系统启动后的秒数转换为墙上时间
    
    参数:
    uptime_seconds: 从系统启动后的秒数
    
    返回:
    对应的墙上时间字符串
    """
    boot_time = get_boot_time()
    wall_timestamp = boot_time + float(uptime_seconds)
    return datetime.datetime.fromtimestamp(wall_timestamp).strftime("%Y-%m-%d %H:%M:%S")

def main():
    while True:
        print("\n选择转换方式:")
        print("1. 墙上时间 -> uptime")
        print("2. uptime -> 墙上时间")
        print("3. 退出程序")
        
        choice = input("请输入选项 (1/2/3): ").strip()
        
        if choice == "3":
            print("程序退出")
            break
            
        elif choice == "1":
            while True:
                wall_time = input("\n请输入墙上时间 (格式: YYYY-MM-DD HH:MM:SS), 或输入'q'返回主菜单: ").strip()
                if wall_time.lower() == 'q':
                    break
                    
                try:
                    uptime = wall_to_uptime(wall_time)
                    print(f"对应的uptime为: {uptime:.6f}")
                except Exception as e:
                    print(f"转换出错: {e}")
                
        elif choice == "2":
            while True:
                uptime_input = input("\n请输入uptime秒数 (如 9809.177025), 或输入'q'返回主菜单: ").strip()
                if uptime_input.lower() == 'q':
                    break
                    
                try:
                    wall_time = uptime_to_wall(uptime_input)
                    print(f"对应的墙上时间为: {wall_time}")
                except Exception as e:
                    print(f"转换出错: {e}")
                    
        else:
            print("无效选项，请重新选择")

if __name__ == "__main__":
    try:
        print("时间转换工具")
        print("============")
        main()
    except KeyboardInterrupt:
        print("\n程序被用户中断")
