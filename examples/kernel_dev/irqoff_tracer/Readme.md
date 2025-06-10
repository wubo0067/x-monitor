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

   



