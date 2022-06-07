# bpftrace观察netfilter

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
    nf_nat_ipv4_pre_routing+68
    nf_hook_slow+63
    ip_rcv+145
    __netif_receive_skb_core.constprop.0+1671
    __netif_receive_skb_list_core+310
    netif_receive_skb_list_internal+454
    napi_complete_done+111
    virtnet_poll+478
    __napi_poll+42
    net_rx_action+559
    __softirqentry_text_start+202
    __irq_exit_rcu+173
    common_interrupt+131
    asm_common_interrupt+30
    default_idle+16
    default_idle_call+47
    cpuidle_idle_call+345
    do_idle+123
    cpu_startup_entry+25
    start_kernel+1166
    secondary_startup_64_no_verify+194
]: 8

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
584             const struct nf_hook_entries *e, unsigned int s)
585    {
586        unsigned int verdict;
587        int ret;
588
589        for (; s < e->num_hook_entries; s++) {
590            verdict = nf_hook_entry_hookfn(&e->hooks[s], skb, state);
591            switch (verdict & NF_VERDICT_MASK) {
592            case NF_ACCEPT:
593
```

### 内核module函数定位

上面nf_hook_slow函数使内核代码，而nf_nat_ipv4_out、nf_nat_inet_fn、nft_nat_do_chain这些都在ko中，vmlinux是没有其符号信息的，这样需要加载kernel module。

1. 在系统中找到模块文件，将其解压
   
   ```
   [root@VM-0-8-centos Program]# modinfo nft_chain_nat
   filename:       /lib/modules/5.14.0-55.el9.x86_64/kernel/net/netfilter/nft_chain_nat.ko.xz
   [root@VM-0-8-centos Program]# xz -d nft_chain_nat.ko.xz
   ```

2. 找到module的.text地址，在目录/sys/module/<module_name>/sections
   
   ```
   [root@VM-0-8-centos sections]# cat .text
   0xffffffffc07e8000
   ```

3. gdb加载模块符号
   
   ```
   (gdb) add-symbol-file /root/Program/nft_chain_nat.ko 0xffffffffc07e8000
   add symbol table from file "/root/Program/nft_chain_nat.ko" at
       .text_addr = 0xffffffffc07e8000
   (y or n) y
   Reading symbols from /root/Program/nft_chain_nat.ko...
   Downloading separate debug info for /root/Program/nft_chain_nat.ko...
   Reading symbols from /root/.cache/debuginfod_client/5acdf22341f7178739c6b3c3cf804e739dd7cf44/debuginfo...
   ```

4. 确定函数的代码位置，有完整代码输出。
   
   ```
   (gdb) p nft_nat_do_chain
   $3 = {unsigned int (void *, struct sk_buff *, const struct nf_hook_state *)} 0xffffffffc07e8020 <nft_nat_do_chain>
   (gdb) list *(nft_nat_do_chain)
   0xffffffffc07e8020 is in nft_nat_do_chain (net/netfilter/nft_chain_nat.c:12).
   7    #include <net/netfilter/nf_tables_ipv4.h>
   8    #include <net/netfilter/nf_tables_ipv6.h>
   9
   10    static unsigned int nft_nat_do_chain(void *priv, struct sk_buff *skb,
   11                         const struct nf_hook_state *state)
   12    {
   13        struct nft_pktinfo pkt;
   14
   15        nft_set_pktinfo(&pkt, skb, state);
   ```

5. 源码编译的vmlinux安装路径
   
   ```
   /lib/modules/4.18.0/build/vmlinux
   ```
   
   使用gdb读取
   
   ```
   [root@localhost build]# gdb /lib/modules/4.18.0/build/vmlinux
   GNU gdb (GDB) Red Hat Enterprise Linux 8.2-18.0.1.el8
   Copyright (C) 2018 Free Software Foundation, Inc.
   License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
   This is free software: you are free to change and redistribute it.
   There is NO WARRANTY, to the extent permitted by law.
   Type "show copying" and "show warranty" for details.
   This GDB was configured as "x86_64-redhat-linux-gnu".
   Type "show configuration" for configuration details.
   For bug reporting instructions, please see:
   <http://www.gnu.org/software/gdb/bugs/>.
   Find the GDB manual and other documentation resources online at:
       <http://www.gnu.org/software/gdb/documentation/>.
   
   For help, type "help".
   Type "apropos word" to search for commands related to "word"...
   Reading symbols from /lib/modules/4.18.0/build/vmlinux...done.
   ```

### 使用bpftrace观察nft_do_chain

1. 使用podman构造验证环境，启动连个容器，nat端口不同。
   
   ```
   [root@localhost calmwu]# podman --runtime /usr/bin/crun run -d -p 8080:80 nginx
   63185f11e26b8a1075b8e2403b1819a40288780acca1ab9453fa8c1417ff572b
   [root@localhost calmwu]# podman --runtime /usr/bin/crun run -d -p 9080:80 nginx
   bf5636ceef9bf16a655e47b40e3f5d7cb5044da6caea2650b288d927f31ef046
   ```

2. 加载nft_do_chain函数对应的模块，读取符号表
   
   ```
   add-symbol-file /lib/modules/4.18.0/kernel/net/netfilter/nf_tables.ko 0xffffffffc0a24000
   ```

3. disassemble，显示代码和指令的对应。使用/s或/m，前者更详细，后者看起来更清晰。
   
   ```
   (gdb) disassemble /s nft_do_chain
   Dump of assembler code for function nft_do_chain:
   net/netfilter/nf_tables_core.c:
   152    {
      0xffffffffc0a24080 <+0>:    callq  0xffffffffc0a24085 <nft_do_chain+5>
      0xffffffffc0a24085 <+5>:    push   %rbp
      0xffffffffc0a24086 <+6>:    mov    %rsp,%rbp
      0xffffffffc0a24089 <+9>:    push   %r15
      0xffffffffc0a2408b <+11>:    push   %r14
      0xffffffffc0a2408d <+13>:    push   %r13
      0xffffffffc0a2408f <+15>:    push   %r12
      0xffffffffc0a24091 <+17>:    mov    %rdi,%r12
      0xffffffffc0a24094 <+20>:    push   %rbx
      0xffffffffc0a24095 <+21>:    and    $0xfffffffffffffff0,%rsp
      0xffffffffc0a24099 <+25>:    sub    $0x1b0,%rsp
      0xffffffffc0a240a0 <+32>:    mov    %rsi,0x8(%rsp)
      0xffffffffc0a240a5 <+37>:    mov    %gs:0x28,%rax
      0xffffffffc0a240ae <+46>:    mov    %rax,0x1a8(%rsp)
      0xffffffffc0a240b6 <+54>:    xor    %eax,%eax
   
   ./include/net/netfilter/nf_tables.h:
   31        return pkt->xt.state->net;
      0xffffffffc0a240b8 <+56>:    mov    0x20(%rdi),%rax
      0xffffffffc0a240bc <+60>:    mov    0x20(%rax),%rax
   
   ./include/linux/compiler.h:
   276        __READ_ONCE_SIZE;
      0xffffffffc0a240c0 <+64>:    movb   $0x0,0x4d(%rsp)
      0xffffffffc0a240c5 <+69>:    movzbl 0x12ec(%rax),%eax
      0xffffffffc0a240cc <+76>:    mov    %al,0x13(%rsp)
   
   ./arch/x86/include/asm/jump_label.h:
   38        asm_volatile_goto("1:"
      0xffffffffc0a240d0 <+80>:    nopl   0x0(%rax,%rax,1)
   
   net/netfilter/nf_tables_core.c:
   168        if (genbit)
      0xffffffffc0a240d5 <+85>:    mov    0x8(%rsp),%rax
      0xffffffffc0a240da <+90>:    cmpb   $0x0,0x13(%rsp)
      0xffffffffc0a240df <+95>:    movl   $0x0,0x14(%rsp)
      0xffffffffc0a240e7 <+103>:    mov    %rax,0x18(%rsp)
      0xffffffffc0a240ec <+108>:    mov    0x18(%rsp),%rax
      0xffffffffc0a240f1 <+113>:    je     0xffffffffc0a243a5 <nft_do_chain+805>
   
   ```

4. 使用kprobe偏移来观察nft_do_chain
   
   ```
   BPFTRACE_VMLINUX=/lib/modules/4.18.0/kernel/net/netfilter/nf_tables.ko bpftrace -v ./trace_pkg_in_netfilter.bt
   ```

5.  bpftrace观察的寄存器名
   
   [bpftrace/x86_64.cpp at master · ajor/bpftrace (github.com)](https://github.com/ajor/bpftrace/blob/master/src/arch/x86_64.cpp)

### 资料

- [The Kernel Newbie Corner: Kernel and Module Debugging with gdb - Linux.com](https://www.linux.com/training-tutorials/kernel-newbie-corner-kernel-and-module-debugging-gdb/)

- https://gist.github.com/jarun/ea47cc31f1b482d5586138472139d090

- [bpftrace/reference_guide.md at master · iovisor/bpftrace (github.com)](https://github.com/iovisor/bpftrace/blob/master/docs/reference_guide.md#1-kprobekretprobe-dynamic-tracing-kernel-level)

- [Kernel analysis with bpftrace [LWN.net\]](https://lwn.net/Articles/793749/)
