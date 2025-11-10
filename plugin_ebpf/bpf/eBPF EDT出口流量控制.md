# eBPF EDT出口流量控制

## 传统方案

​		在linux系统上我们如果需要对一个服务的出口带宽进行限制会使用TC HTB来实现。但HTB对网卡的多队列支持不好，从下图可以看到，虽然HTB会创建4个软队列，但是4个软队列会共享同一个HTB策略，这样流控策略会在4个cpu上产生竞争。

​		HTB是全局共享的。

​		![image-20251110103507796](./image-20251110103507796.png)

​		如果使用mq Qdisc + HTB child Qdisc模式，那么就需要对应用发出的SKB做qdisc soft queue绑定了。这个特性虽然可以使用eBPF的queue_mapping来实现，但实际使用，配置，运维起来十分的麻烦。

## EDT方案

​		EDT：Earliset Departure Time，核心是按用户设置的Rate bps给发送的skb打上发送时间戳，配合fq qdisc的timeing wheel scheduler来发包。

![image-20251110110740386](./image-20251110110740386.png)

​		用eBPF来实现EDT有两种方式：

### eBPF cgroup_skb/egress

​		**在网络三层工作，使用eBPF cgroup_skb/egress Prog，hook点是BPF_CGROUP_INET_EGRESS**。这种方式的出处来自于内核源码samples/bpf/hbm_edt_kern.c，好处在于将应用和cgroup对应，配置方便。prog的返回值可以在感知拥塞的情况下启动ecn通知和cwnd的拥塞恢复。

### eBPF TC qdisc

​		**在网络二层工作，使用eBPF tc Prog，hook点是SCHED_CLS，需要使用tc qdisc add dev $dev clasct, 在添加filter**。这种方式被cillium使用，用来限制每个Pod的出口带宽。该Prog需要在逻辑中区别Pod的endpoint，针对不同的endpoint来使用不用的rate bps。

​		本程序是运行在物理机上，用来限制某个具体的应用（例如：redis）出口流量，所以选择的是cgroup_skb/egress，工作在IP层。实现的源码：[x-monitor/plugin_ebpf/bpf/xm_hbm_edt.bpf.c at main · wubo0067/x-monitor](https://github.com/wubo0067/x-monitor/blob/main/plugin_ebpf/bpf/xm_hbm_edt.bpf.c)

## 使用