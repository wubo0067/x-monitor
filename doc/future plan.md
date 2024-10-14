1. 对 sock 的状态采集，输出类似 ss -itmp 的数据，需要使用 netlink 从内核获取
2. 对 softnet_data 的数据采集
3. 对 skb 的 tracepoint 采集，主要是 skb 的 drop，堆积的相关
4. xfs 的 tracepoint 采集，tx-queue、rx-queue 队列堆积数量
