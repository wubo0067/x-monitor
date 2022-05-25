# 使用bpftrace获取内核函数堆栈，定位内核代码

### 获取内核函数nft_nat_do_chain的调用堆栈

使用bfptrace脚本来获取内核堆栈

```
bpftrace -e 'kprobe:nft_nat_do_chain { @[kstack] = count(); }'
```

堆栈输出信息：

```
@[
    nft_nat_do_chain+1
    nf_nat_inet_fn+318
    nf_nat_ipv4_out+77
    nf_hook_slow+63
    ip_output+220
    __ip_queue_xmit+354
    __tcp_transmit_skb+2192
    tcp_connect+957
    tcp_v4_connect+946
    __inet_stream_connect+202
    inet_stream_connect+55
    __sys_connect+159
    __x64_sys_connect+20
    do_syscall_64+59
    entry_SYSCALL_64_after_hwframe+68
]: 18
```

### 获取栈帧对应的内核代码

这里想知道nf_hook_slow+63具体是内核的代码那一行。这里要获取该版本带有的debuginfo的vmlinux。使用如下命令：

```
wget http://linuxsoft.cern.ch/cern/centos/s9/BaseOS/x86_64/debug/tree/Packages/kernel-debug-debuginfo-5.14.0-55.el9.x86_64.rpm
wget http://linuxsoft.cern.ch/cern/centos/s9/BaseOS/x86_64/debug/tree/Packages/kernel-debug-debuginfo-common-5.14.0-55.el9.x86_64.rpm
```

安装的vmlinux位于：/usr/lib/debug/lib/modules/5.14.0-55.el9.x86_64+debug/vmlinux

执行下面命令都可以获取函数nf_hook_slow的基地址，ffffffff82bbe0c0

- nm命令

  ```
  [root@VM-0-8-centos Program]# nm -A /usr/lib/debug/lib/modules/5.14.0-55.el9.x86_64+debug/vmlinux|grep -w nf_hook_slow
  /usr/lib/debug/lib/modules/5.14.0-55.el9.x86_64+debug/vmlinux:ffffffff82bbe0c0 T nf_hook_slow
  ```

- readelf

  ```
  [root@VM-0-8-centos Program]# readelf -sW /usr/lib/debug/lib/modules/5.14.0-55.el9.x86_64+debug/vmlinux|grep -w nf_hook_slow
  115025: ffffffff82bbe0c0   396 FUNC    GLOBAL DEFAULT    1 nf_hook_slow
  ```

  使用基地址+0x3F(63)偏移 = FFFFFFFF82BBE0FF

**使用addr2line定位到对应代码:**

```
[root@VM-0-8-centos Program]# addr2line -e /usr/lib/debug/lib/modules/5.14.0-55.el9.x86_64+debug/vmlinux FFFFFFFF82BBE0FF
/usr/src/debug/kernel-5.14.0-55.el9/linux-5.14.0-55.el9.x86_64/net/netfilter/core.c:589
```

**使用gdb更快获取对应代码：**

```
[root@VM-0-8-centos Program]# gdb /usr/lib/debug/lib/modules/5.14.0-55.el9.x86_64+debug/vmlinux

(gdb) list *(nf_hook_slow+0x3f)
0xffffffff82bbe0ff is in nf_hook_slow (net/netfilter/core.c:589).
584			 const struct nf_hook_entries *e, unsigned int s)
585	{
586		unsigned int verdict;
587		int ret;
588	
589		for (; s < e->num_hook_entries; s++) {
590			verdict = nf_hook_entry_hookfn(&e->hooks[s], skb, state);
591			switch (verdict & NF_VERDICT_MASK) {
592			case NF_ACCEPT:
593	
```

