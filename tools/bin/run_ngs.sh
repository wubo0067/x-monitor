#!/bin/bash

# 定义基础变量
BASE_NAME="vm"  # 容器名称前缀
BASE_PORT=80    # 起始端口
CONTAINER_COUNT=25  # 容器数量
VOLUME_SOURCE="/root/anaconda-ks.cfg"  # 挂载的源文件
VOLUME_TARGET="/calmwu"  # 挂载的目标路径
IMAGE_NAME="nginx"  # 使用的镜像

# 并发启动容器
for ((i=1; i<=CONTAINER_COUNT; i++))
do
    # 计算容器名称和端口
    CONTAINER_NAME="${BASE_NAME}${i}"
    HOST_PORT=$((BASE_PORT + i - 1))  # 端口递增，避免冲突

    # 启动容器（后台执行）
    docker run -d \
        --name "$CONTAINER_NAME" \
        -p "$HOST_PORT:80" \
        -v "$VOLUME_SOURCE:$VOLUME_TARGET" \
        "$IMAGE_NAME" &

    # 记录后台任务的 PID
    PIDS[$i]=$!
    echo "启动容器 $CONTAINER_NAME，映射端口: $HOST_PORT，PID: ${PIDS[$i]}"
done

# 等待所有后台任务完成
for pid in "${PIDS[@]}"
do
    wait "$pid"
    if [ $? -eq 0 ]; then
        echo "容器启动任务 $pid 已完成"
    else
        echo "容器启动任务 $pid 失败"
    fi
done

echo "所有容器启动任务已完成"
