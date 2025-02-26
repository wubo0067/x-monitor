#!/bin/bash

# 定义基础变量
BASE_NAME="vm"  # 容器名称前缀
CONTAINER_COUNT=25  # 容器数量

# 循环停止并删除容器
for ((i=1; i<=CONTAINER_COUNT; i++))
do
    # 计算容器名称
    CONTAINER_NAME="${BASE_NAME}${i}"

    # 停止容器
    docker stop "$CONTAINER_NAME" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "容器 $CONTAINER_NAME 已停止"
    else
        echo "容器 $CONTAINER_NAME 停止失败（可能不存在或已停止）"
    fi

    # 删除容器
    docker rm "$CONTAINER_NAME" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "容器 $CONTAINER_NAME 已删除"
    else
        echo "容器 $CONTAINER_NAME 删除失败（可能不存在）"
    fi
done
