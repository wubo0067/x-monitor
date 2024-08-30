1. 对sock的状态采集，输出类似ss -itmp的数据，需要使用netlink从内核获取
2. 对softnet_data的数据采集
3. 对skb的tracepoint采集，主要是skb的drop，堆积的相关
4. xfs的tracepoint采集，tx-queue、rx-queue队列堆积数量