#!/bin/bash
# tcp_health_monitor.sh - TCP连接健康监控

LC_ALL=C

echo "=== TCP拥塞控制监控 ==="
echo "时间戳,连接数,平均cwnd,零窗口丢包数"

while true; do
    timestamp=$(date '+%H:%M:%S')

    # 连接数（去掉表头 -H，更稳健）
    conn_count=$(ss -ntH state established | wc -l)

    # 平均 cwnd：从 ss -tiH 的 key:value 对里提取 cwnd:NN
    avg_cwnd=$(
      ss -tiH state established \
      | awk '{
          for (i=1; i<=NF; i++) {
            if ($i ~ /cwnd:/) {
              gsub(/,/, "", $i);              # 去掉逗号
              split($i, a, ":");              # a[1]=cwnd a[2]=数值
              if (a[2] ~ /^[0-9]+$/) { sum+=a[2]; cnt++ }
            }
          }
        }
        END { if (cnt>0) printf("%.2f", sum/cnt); else print 0 }'
    )

    # ZeroWindowDrop：解析 /proc/net/netstat 的 TcpExt 表头和值
    zero_win_drops=$(
      awk '
        BEGIN{FS=" "}
        /^TcpExt:/ {
          if (hdr_read==0) {                   # 第一次 TcpExt: 为表头
            for (i=2; i<=NF; i++) hdr[i-1]=$i;
            hdr_read=1; next;
          } else if (hdr_read==1) {            # 紧跟的下一行是对应数值
            for (i=2; i<=NF; i++) val[i-1]=$i;
            hdr_read=2;
          }
        }
        END {
          if (hdr_read<2) { print 0; exit }
          for (i in hdr) {
            if (hdr[i]=="TCPZeroWindowDrop") {
              print (i in val ? val[i] : 0);
              exit
            }
          }
          print 0
        }' /proc/net/netstat
    )

    echo "$timestamp,$conn_count,$avg_cwnd,$zero_win_drops"
    sleep 2
done