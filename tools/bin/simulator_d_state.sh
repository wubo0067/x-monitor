#!/bin/bash

# 创建测试环境
MNT="/mnt/freeze_test"
IMG="/tmp/freeze.img"
LOOPDEV="/dev/loop10"

# 清理函数，确保在脚本退出时执行
cleanup() {
    local exit_code=$?

    echo "执行清理操作..."

    # 解冻文件系统（如果被冻结）
    fsfreeze -u "$MNT" 2>/dev/null || true

    # 杀掉所有 dd 进程
    if declare -p PIDS >/dev/null 2>&1 && [ ${#PIDS[@]} -gt 0 ]; then
        for pid in "${PIDS[@]}"; do
            if kill -0 "$pid" 2>/dev/null; then
                kill "$pid"
                echo "已终止 dd 进程 PID $pid"
            fi
        done
    fi

    # 等待所有后台进程退出
    wait 2>/dev/null || true

    # 卸载文件系统
    if mountpoint -q "$MNT" 2>/dev/null; then
        umount "$MNT" 2>/dev/null || true
        echo "已卸载 $MNT"
    fi

    # 分离回环设备
    if [ -b "$LOOPDEV" ] && losetup "$LOOPDEV" >/dev/null 2>&1; then
        losetup -d "$LOOPDEV" 2>/dev/null || true
        echo "已分离 $LOOPDEV"
    fi

    # 清理临时文件和目录
    rm -f "$IMG"
    rmdir "$MNT" 2>/dev/null || true

    echo "清理完成"
    exit $exit_code
}

# 注册清理函数处理各种退出情况
trap cleanup EXIT
trap cleanup ERR
trap cleanup INT
trap cleanup TERM

# 初始化 PIDS 数组
PIDS=()

# 检查并清理可能存在的旧文件
if [ -f "$IMG" ]; then
    echo "发现旧的镜像文件 $IMG，尝试清理..."
    # 检查是否有回环设备正在使用这个镜像文件
    USED_LOOPDEV=$(losetup -j "$IMG" 2>/dev/null | head -n1 | cut -d: -f1)
    if [ -n "$USED_LOOPDEV" ]; then
        echo "发现 $IMG 被 $USED_LOOPDEV 使用，尝试分离..."
        losetup -d "$USED_LOOPDEV" 2>/dev/null || true
        sleep 1
    fi
    rm -f "$IMG"
fi

# 创建一个 100MB 的文件并格式化为 ext4
echo "创建 100MB 镜像文件..."
dd if=/dev/zero of="$IMG" bs=1M count=100

# 检查回环设备是否存在，如果不存在则创建
if [ ! -b "$LOOPDEV" ]; then
    echo "回环设备 $LOOPDEV 不存在，尝试创建..."
    # 检查系统是否支持创建回环设备
    if command -v mknod >/dev/null 2>&1; then
        # 尝试创建回环设备节点 (主设备号 7，次设备号 10)
        if mknod "$LOOPDEV" b 7 10 2>/dev/null; then
            echo "成功创建回环设备 $LOOPDEV"
        else
            echo "错误: 无法创建回环设备 $LOOPDEV，权限不足或设备号已被占用"
            exit 1
        fi
    else
        echo "错误: 系统不支持创建回环设备，且 $LOOPDEV 不存在"
        exit 1
    fi
else
    echo "回环设备 $LOOPDEV 已存在"
fi

# 检查设备是否已被使用，如果使用则分离
if losetup "$LOOPDEV" >/dev/null 2>&1; then
    echo "警告: $LOOPDEV 已被使用，尝试分离..."
    if losetup -d "$LOOPDEV" 2>/dev/null; then
        echo "成功分离已使用的 $LOOPDEV"
        # 等待设备完全释放
        sleep 1
    else
        echo "错误: 无法分离已使用的 $LOOPDEV"
        exit 1
    fi
fi

echo "设置回环设备 $LOOPDEV..."
if ! losetup "$LOOPDEV" "$IMG"; then
    echo "错误: 无法将 $IMG 设置到 $LOOPDEV"
    # 尝试使用 losetup -f 来自动分配设备
    echo "尝试使用自动分配的回环设备..."
    LOOPDEV=$(losetup -f --show "$IMG" 2>/dev/null)
    if [ -z "$LOOPDEV" ]; then
        echo "错误: 无法使用自动分配的回环设备"
        exit 1
    else
        echo "成功使用自动分配的设备: $LOOPDEV"
    fi
fi

if ! mkfs.ext4 -F "$LOOPDEV"; then
    echo "错误: 无法格式化 $LOOPDEV"
    exit 1
fi

# 挂载文件系统
echo "挂载文件系统到 $MNT..."
mkdir -p "$MNT"
if ! mount "$LOOPDEV" "$MNT"; then
    echo "错误: 无法挂载 $LOOPDEV 到 $MNT"
    exit 1
fi

# 冻结文件系统
echo "冻结文件系统..."
if ! fsfreeze -f "$MNT"; then
    echo "错误: 无法冻结文件系统 $MNT"
    exit 1
fi
echo "文件系统已冻结于 $MNT"

# 启动阻塞写入任务（将进入 D 状态）
echo "启动阻塞写入任务..."
for i in {1..5}; do
    dd if=/dev/zero of="$MNT/file$i" bs=1M count=10 &
    PIDS+=($!)
    echo "已启动 dd 进程 $i (PID ${PIDS[-1]})"
done

echo "使用 'ps -eo pid,stat,wchan,comm | grep ^D' 查看 D 状态进程"
echo "等待 60 秒..."
sleep 60

# 解冻文件系统
echo "解冻文件系统..."
fsfreeze -u "$MNT"
echo "文件系统已解冻"

# 杀掉所有 dd 进程
echo "终止 dd 进程..."
for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid"
        echo "已终止 dd 进程 PID $pid"
    fi
done

# 等待所有进程退出
wait

echo "脚本执行完成"