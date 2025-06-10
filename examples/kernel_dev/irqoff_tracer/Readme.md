# 使用说明

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

   



