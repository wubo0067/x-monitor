#!/bin/bash

# 设置默认的循环次数
DEFAULT_LOOPS=5

# 获取传入的循环次数，如果没有则使用默认值
LOOPS=${1:-$DEFAULT_LOOPS}

# 获取传入的IP:PORT参数，默认使用原来的地址
IP_PORT=${2:-"192.168.14.46:8000"}

# 确保循环次数是正整数
if ! [[ "$LOOPS" =~ ^[1-9][0-9]*$ ]]; then
  echo "错误: 循环次数必须是正整数。"
  echo "用法: $0 [循环次数] [IP:PORT]"
  exit 1
fi

# 验证IP:PORT格式
if ! [[ "$IP_PORT" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}:[0-9]+$ ]]; then
  echo "错误: IP:PORT格式不正确，应为xxx.xxx.xxx.xxx:port格式。"
  echo "用法: $0 [循环次数] [IP:PORT]"
  exit 1
fi

# 构造完整的URL
URL="http://$IP_PORT"

# 循环调用服务器
echo "开始循环调用服务器 $LOOPS 次..."
echo "目标地址: $IP_PORT"
for ((i=1; i<=$LOOPS; i++)); do
  echo "第 $i 次调用..."
  curl -sS "$URL" > /dev/null
  echo ""

  # 每10个curl请求后sleep 1秒
  if (( i % 10 == 0 && i < LOOPS )); then
    echo "已完成 $i 次调用，休眠1秒..."
    sleep 1
  fi
done

echo "完成。"