import subprocess
from datetime import datetime, timedelta

def get_system_start_time():
    """获取系统启动时间，格式为 datetime 对象"""
    try:
        # 执行 `uptime -s` 获取启动时间
        output = subprocess.check_output(["uptime", "-s"]).decode("utf-8").strip()
        return datetime.strptime(output, "%Y-%m-%d %H:%M:%S")
    except Exception as e:
        print("获取系统启动时间失败:", e)
        exit(1)

def convert_relative_to_wall_time(relative_times):
    """将多个相对时间转换为墙上时间"""
    start_time = get_system_start_time()
    wall_times = []
    for relative_time in relative_times:
        wall_time = start_time + timedelta(seconds=relative_time)
        wall_times.append(wall_time)
    return wall_times

if __name__ == "__main__":
    # 输入相对时间
    try:
        input_times = input("请输入相对时间戳（秒），用逗号分隔：")
        relative_times = [float(ts.strip()) for ts in input_times.split(",")]
        wall_times = convert_relative_to_wall_time(relative_times)
        for relative_time, wall_time in zip(relative_times, wall_times):
            print(f"相对时间: {relative_time:.6f} 秒 -> 墙上时间: {wall_time}")
    except ValueError:
        print("输入的时间戳格式无效，请确保是逗号分隔的数字！")

