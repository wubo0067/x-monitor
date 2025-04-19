#!/bin/bash

# 设置默认的循环次数
DEFAULT_LOOPS=5

# 获取传入的循环次数，如果没有则使用默认值
LOOPS=${1:-$DEFAULT_LOOPS}

# 确保循环次数是正整数
if ! [[ "$LOOPS" =~ ^[1-9][0-9]*$ ]]; then
  echo "错误: 循环次数必须是正整数。"
  echo "用法: $0 [循环次数]"
  exit 1
fi

# 循环调用服务器
echo "开始循环调用服务器 $LOOPS 次..."
for ((i=1; i<=$LOOPS; i++)); do
  echo "第 $i 次调用..."
  curl -sS http://192.168.2.128:8000 > /dev/null
  echo ""
  #sleep 0.5 # 可选的延迟
done

echo "完成。"