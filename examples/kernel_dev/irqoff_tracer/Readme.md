# irqoff-tracer

## 背景

### 硬中断延迟

1. **中断嵌套限制**：在大多数系统中，当CPU正在执行一个硬中断处理程序时，其他硬中断通常会被暂时禁用或延迟。这是为了避免中断嵌套过深导致栈溢出。
2. **关中断操作**：代码中的临界区可能会临时关闭中断，导致所有硬中断被延迟。
3. **中断嵌套：**Linux内核默认对硬中断采用"**禁止嵌套**"的方式。当CPU进入硬中断处理程序时，会自动禁用本地CPU上的所有中断。当然**NMI**例外。
4. **hrtimer定时器**：使用硬中断，如果当前CPU正在处理另一个硬中断，hrtimer的中断确实会被延迟到当前中断处理完成后才能执行。延迟时间取决于正在执行的中断处理程序的执行时间。

### 场景

一个场景，NET_RX_SOFTIRQ正在ksoftirqd内核进程中执行，这个时候是否会被网卡接收中断打断？如果后续继续来了hrtimer定时器中断，那么是否要等到hrtimer中断isr执行完毕后才能恢复ksoftirqd的运行继续处理net_rx_softirq的逻辑？

结论：

1. 当 `NET_RX_SOFTIRQ` 是在 **ksoftirqd** 线程里运行时，CPU 上的**本地硬中断是开着的**，因此**网卡新的接收硬中断可以随时打断 ksoftirqd**。

2. 如果紧接着又来了高精度定时器（hrtimer）的硬中断，那么 ksoftirqd 会继续被抢占，**必须等到 hrtimer 的硬中断处理（ISR + irq_exit）全部结束后**，ksoftirqd 才有机会被调度回 CPU 继续执行剩余的 `NET_RX_SOFTIRQ` 代码。

   ```
   ksoftirqd:    |-- NET_RX_SOFTIRQ handler --(IRQ允许)--•─────────────┐
                                                             ↑
   NIC IRQ:      ·······························→ [ISR] ... irq_exit ...|
                                                             ↑
   hrtimer IRQ:  ···········································→ [ISR] ... irq_exit
                                                             ↑
   ksoftirqd:    └──────────── resumes here ─────────────────•───────────>
   
   ```

### 实现

1. 在hrtimer的isr中调用stack_trace_save_regs和stack_trace_save。
2. current不变。在硬中断里它仍指向被抢占的任务，因此栈属于 ksoftirqd；这也是在 IRQ 中访问 `current` 时得到的内容。
3. 输出内容：**最上面几行是中断路径，随后就是 ksoftirqd 的真实业务调用栈。**

| 步骤                                              | 发生了什么                                                   |
| ------------------------------------------------- | ------------------------------------------------------------ |
| 1. hrtimer 到期                                   | 触发本地时钟 IRQ，CPU 进入 **硬中断上下文**，把被抢占的寄存器保存在 `struct pt_regs` 中；`current` 仍指向 **ksoftirqd**（内核不会做任务切换）。 |
| 2. 可能切到 IRQ-stack                             | 在 x86-64 等架构上，硬中断会从 ksoftirqd 的线程栈切到每-CPU 的 **interrupt stack**。 |
| 3. 在 ISR 里调用 `stack_trace_save_regs(regs, …)` | 该函数内部调用 `arch_stack_walk(…, current, regs)`，明确告诉 unwinder：“从 `regs->sp/ip` 对应的 **被中断现场** 开始往上走”。 |
| 4. 栈回溯结果                                     | 顶部几行是 `hrtimer_interrupt → __hrtimer_run_queues → 你的_isr` 之类的帧，随后无缝跨栈回到 ksoftirqd 正在执行的函数（如 `net_rx_action()`），一直到 kthread 入口为止。 |

## 编译、安装

1. 编译

   ```
   make noisy
   sudo make install
   ```

   ```
   make ENABLE_FOR_TEST=1 noisy  # 编译时启用 FOR_TEST
   make ENABLE_FOR_TEST=1 install # 安装时也启用 FOR_TEST
   ```

## 启动关闭irqoff tracing

1. 开启

   ```
   ⚡ root@localhost  ~  echo 1 > /proc/irqoff_tracer/enabled
   [Tue Jun 10 17:09:02 2025] cw_irqoff_tracer:__irqoff_tracer_enabled_write(): Module:[cw_irqoff_tracer] enabling tracing.
   ```

2. 关闭

   ```
   ⚡ root@localhost  ~  echo 0 > /proc/irqoff_tracer/enabled
   [Tue Jun 10 17:12:12 2025] cw_irqoff_tracer:__irqoff_tracer_enabled_write(): Module:[cw_irqoff_tracer] disabling tracing.
   ```

3. 卸载内核模块

   ```
   ⚡ root@localhost  ~   rmmod cw_irqoff_tracer
   [Tue Jun 10 17:26:27 2025] cw_irqoff_tracer:__cw_irqoff_tracer_exit(): Module:[cw_irqoff_tracer] exited.
   ```

## 采样频率

1. 查看采样频率

   ```
    ⚡ root@localhost  ~  cat /proc/irqoff_tracer/sampling_period 
   10ms
   ```

2. 设置采样频率

   ```
   ⚡ root@localhost  ~  echo 5 > /proc/irqoff_tracer/sampling_period
   [Tue Jun 10 17:20:15 2025] cw_irqoff_tracer:__irqoff_tracer_sampling_period_write(): Module:[cw_irqoff_tracer] updated sampling period:5ms, irqoff trace latency:50ms
   ```

## 延迟分布

1. 查看直方图

   ```
    ? root@localhost ? ~ ?  cat /proc/irqoff_tracer/histogram
   Hardirq-off Latency Histogram:
   
            msecs           : count     distribution
           10 -> 19         : 4838     |========================================|
           20 -> 39         : 319      |==                                      |
           40 -> 79         : 23       |                                        |
   Softirq-off Latency Histogram:
   
            msecs           : count     distribution
           10 -> 19         : 3        |========================================|
   ```

2. 清空直方图

   ```
    ⚡ root@localhost  ~                                       
   echo 0 > /proc/irqoff_tracer/latency
    ⚡ root@localhost  ~   cat /proc/irqoff_tracer/histogram
   Hardirq-off Latency Histogram:
   
            msecs           : count     distribution
           10 -> 19         : 3        |========================================|
           20 -> 39         : 2        |==========================              |
   Softirq-off Latency Histogram:
    No data available.
   ```

## 设置irqoff超时阈值

1. 设置

   ```
   ⚡ root@localhost  ~  echo 10 > /proc/irqoff_tracer/latency
   [Tue Jun 10 17:57:36 2025] cw_irqoff_tracer:__irqoff_tracer_latency_write(): Module:[cw_irqoff_tracer] set new latency threshold: 10ms
   ```

## irqoff超时堆栈

1. 查看

   ```
    ⚡ root@localhost  ~  cat /proc/irqoff_tracer/latency
   irqoff_tracer_latency: 10ms
   
    hardirq:
   CPU 0
        COMMAND: swapper/0 PID: 0 LATENCY: 11ms
        __irqoff_tracer_record+0x1ff/0x210 [cw_irqoff_tracer]
        __irqoff_tracer_hrtimer_callback+0x49/0xeb [cw_irqoff_tracer]
        __hrtimer_run_queues+0x101/0x280
        hrtimer_interrupt+0x100/0x220
        smp_apic_timer_interrupt+0x6a/0x130
        apic_timer_interrupt+0xf/0x20
        native_safe_halt+0xe/0x20
        acpi_idle_do_entry+0x53/0x70
        acpi_idle_enter+0x5a/0xd0
        cpuidle_enter_state+0x86/0x3d0
        cpuidle_enter+0x2c/0x40
        do_idle+0x268/0x2d0
        cpu_startup_entry+0x6f/0x80
        start_kernel+0x522/0x546
        secondary_startup_64_no_verify+0xc2/0xcb
   
        COMMAND: swapper/0 PID: 0 LATENCY: 14ms
        __irqoff_tracer_record+0x1ff/0x210 [cw_irqoff_tracer]
        __irqoff_tracer_hrtimer_callback+0x49/0xeb [cw_irqoff_tracer]
        __hrtimer_run_queues+0x101/0x280
        hrtimer_interrupt+0x100/0x220
        smp_apic_timer_interrupt+0x6a/0x130
        apic_timer_interrupt+0xf/0x20
        native_safe_halt+0xe/0x20
        acpi_idle_do_entry+0x53/0x70
        acpi_idle_enter+0x5a/0xd0
        cpuidle_enter_state+0x86/0x3d0
        cpuidle_enter+0x2c/0x40
        do_idle+0x268/0x2d0
        cpu_startup_entry+0x6f/0x80
        start_kernel+0x522/0x546
        secondary_startup_64_no_verify+0xc2/0xcb
   
   CPU 1
        COMMAND: swapper/1 PID: 0 LATENCY: 11ms
        __irqoff_tracer_record+0x1ff/0x210 [cw_irqoff_tracer]
        __irqoff_tracer_hrtimer_callback+0x49/0xeb [cw_irqoff_tracer]
        __hrtimer_run_queues+0x101/0x280
        hrtimer_interrupt+0x100/0x220
        smp_apic_timer_interrupt+0x6a/0x130
        apic_timer_interrupt+0xf/0x20
        native_safe_halt+0xe/0x20
        acpi_idle_do_entry+0x53/0x70
        acpi_idle_enter+0x5a/0xd0
        cpuidle_enter_state+0x86/0x3d0
        cpuidle_enter+0x2c/0x40
        do_idle+0x268/0x2d0
        cpu_startup_entry+0x6f/0x80
        start_secondary+0x18c/0x1d0
        secondary_startup_64_no_verify+0xc2/0xcb
   
        COMMAND: swapper/1 PID: 0 LATENCY: 10ms
        __irqoff_tracer_record+0x1ff/0x210 [cw_irqoff_tracer]
        __irqoff_tracer_hrtimer_callback+0x49/0xeb [cw_irqoff_tracer]
        __hrtimer_run_queues+0x101/0x280
        hrtimer_interrupt+0x100/0x220
        smp_apic_timer_interrupt+0x6a/0x130
        apic_timer_interrupt+0xf/0x20
        native_safe_halt+0xe/0x20
        acpi_idle_do_entry+0x53/0x70
        acpi_idle_enter+0x5a/0xd0
        cpuidle_enter_state+0x86/0x3d0
        cpuidle_enter+0x2c/0x40
        do_idle+0x268/0x2d0
        cpu_startup_entry+0x6f/0x80
        start_secondary+0x18c/0x1d0
        secondary_startup_64_no_verify+0xc2/0xcb
   ```

2. 清理堆栈

   ```
    ⚡ root@localhost  ~  echo 0 > /proc/irqoff_tracer/latency 
    ⚡ root@localhost  ~  cat /proc/irqoff_tracer/latency     
   irqoff_tracer_latency: 10ms
   
    hardirq:
   
    softirq:
   ```


## 测试

1. 软中断延迟，写入s字符到for_test，关闭中断下半部100ms

   ```
           local_bh_disable();
           // 延迟 100ms
           mdelay(100);
           local_bh_enable();
   ```

   捕捉

   ```
    ⚡ root@localhost  ~  echo 1 > /proc/irqoff_tracer/enabled                                                                 
    ⚡ root@localhost  ~  echo s > /proc/irqoff_tracer/for_test
    ⚡ root@localhost  ~  cat /proc/irqoff_tracer/latency      
   irqoff_tracer_latency: 50ms, sampling_period: 10ms
   
    hardirq:
   
    softirq:
   CPU 0
        COMMAND: zsh PID: 110013 LATENCY: 67+ms
        delay_tsc+0x20/0x50
        __irqoff_tracer_test_write.cold.10+0x112/0x171 [cw_irqoff_tracer]
        proc_reg_write+0x39/0x60
        vfs_write+0xa5/0x1b0
        ksys_write+0x4f/0xb0
        do_syscall_64+0x5b/0x1b0
        entry_SYSCALL_64_after_hwframe+0x61/0xc6
   ```

2. 硬中断延迟，写入h字符到for_test，

   ```
           local_irq_disable();
           // 延迟 100ms
           mdelay(100);
           local_irq_enable();
   ```

   捕捉到硬中断延迟堆栈，看cpu3

   ```
    ⚡ root@localhost  ~  echo h > /proc/irqoff_tracer/for_test
    ⚡ root@localhost  ~  cat /proc/irqoff_tracer/latency      
   irqoff_tracer_latency: 50ms, sampling_period: 10ms
   
    hardirq:
   CPU 2
        COMMAND: swapper/2 PID: 0 LATENCY: 51ms
        __irqoff_tracer_record+0x1ff/0x210 [cw_irqoff_tracer]
        __irqoff_tracer_hrtimer_callback+0x49/0xeb [cw_irqoff_tracer]
        __hrtimer_run_queues+0x101/0x280
        hrtimer_interrupt+0x100/0x220
        smp_apic_timer_interrupt+0x6a/0x130
        apic_timer_interrupt+0xf/0x20
        _raw_spin_unlock_irqrestore+0x11/0x20
        uhci_hub_status_data+0x63/0x21c
        usb_hcd_poll_rh_status+0x5a/0x1a0
        call_timer_fn+0x2e/0x130
        run_timer_softirq+0x1d8/0x410
        __do_softirq+0xdc/0x2cf
        irq_exit_rcu+0xd5/0xe0
        irq_exit+0xa/0x10
        smp_apic_timer_interrupt+0x74/0x130
        apic_timer_interrupt+0xf/0x20
        native_safe_halt+0xe/0x20
        acpi_idle_do_entry+0x53/0x70
        acpi_idle_enter+0x5a/0xd0
        cpuidle_enter_state+0x86/0x3d0
        cpuidle_enter+0x2c/0x40
        do_idle+0x268/0x2d0
        cpu_startup_entry+0x6f/0x80
        start_secondary+0x18c/0x1d0
        secondary_startup_64_no_verify+0xc2/0xcb
   
   CPU 3
        COMMAND: zsh PID: 110013 LATENCY: 108ms
        __irqoff_tracer_record+0x1ff/0x210 [cw_irqoff_tracer]
        __irqoff_tracer_hrtimer_callback+0x49/0xeb [cw_irqoff_tracer]
        __hrtimer_run_queues+0x101/0x280
        hrtimer_interrupt+0x100/0x220
        smp_apic_timer_interrupt+0x6a/0x130
        apic_timer_interrupt+0xf/0x20
        __irqoff_tracer_test_write.cold.10+0x8d/0x171 [cw_irqoff_tracer]
        proc_reg_write+0x39/0x60
        vfs_write+0xa5/0x1b0
        ksys_write+0x4f/0xb0
        do_syscall_64+0x5b/0x1b0
        entry_SYSCALL_64_after_hwframe+0x61/0xc6
   
   ```

   



