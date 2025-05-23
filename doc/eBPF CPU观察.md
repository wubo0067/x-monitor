# eBPF CPU观察

## CPU调度器

系统内核中的调度单元主要是Thread，这些thread也叫Task。其它类型的调度单元还包括中断处理程序：这些可能是软件运行过程中产生的软中断，例如网络收包，也可能是引荐发出的硬中断。

Thread运行的三种状态

- ON-PROC：指正在CPU上运行的线程
- RUNNABLE：指可运行，但正在运行队列中排队等待的线程。CFS调度现在已经是红黑树来维护状态了。
- SLEEP：指正在等待其它事件，包括不可中断的等待线程。

RunQueue中的线程是按照优先级排序的，这个值可以由系统内核和用户进程来设置，通过调节这个值可以调高重要任务的运行性能。

有两种方式可以让线程脱离CPU执行：

- 主动脱离，任务主动通过直接或间接调用schedule函数引起的上下文切换。

- 被动脱离：如果线程运行时长超过调度器分配给其的CPU时间，或者高优先级抢占低优先级任务时，就会被调度器调离CPU。

**上下文切换**：当CPU从一个线程切换到另一个线程时，需要更换内存寻址信息和其它的上下文信息，这种行为就是“上下文切换”。一般操作系统因为以下三种方式引起上下文切换，

- Task Scheduling（进程上下文切换），任务调度一般是由调度器在内核空间完成，通常将当前CPU执行任务的代码，用户或内核栈，地址空间切换到下一个需要运行任务的代码，用户或内核栈，地址空间。这是一种整体切换。成本是比系统调用要高的。

  如果是同一个进程的线程间进行上下文切换，它们之间有一部分共用的数据，这些数据可以免除切换，所以开销较小。

- Interrupt or Exception

- System Call（特权模式切换），**同一进程内**的CPU上下文切换，相对于进程的切换应该少了保存用户空间资源这一步。

进程的上下文包括，虚拟内存，栈，全局变量，寄存器等用户空间资源，还包括内核堆栈、寄存器等内核空间状态。

TLB（加快虚拟地址到物理地址转换速度），全局类型TLB：内核空间时所有进程共享的空间，因此这部分空间的虚拟地址到物理地址的翻译不会变化。进程独有类型的TLB：用户地址空间是每个进程独有的。。

线程迁移：在多core环境中，有core空闲，并且运行队列中有可运行状态的线程等待执行，CPU调度器可以将这个线程迁移到该core的队列中，以便尽快运行。

### 调度类

进程调度依赖调度策略，内核把相同的调度策略抽象成**调度类**。不同类型的进程采用不同的调度策略，Linux内核中默认实现了5个调度类

1. Stop Scheduler，这是最高优先级调度类，用于处理紧急情况，如系统崩溃或关机是需要停止其它进程。

2. Deadline，用于调度有严格时间要求的实时进程，比如视频编解码

   ```
   const struct sched_class dl_sched_class = {
   	.next			= &rt_sched_class,
   	.enqueue_task		= enqueue_task_dl,
   	.dequeue_task		= dequeue_task_dl,
   	.yield_task		= yield_task_dl,
   	.check_preempt_curr	= check_preempt_curr_dl,
   ```

3. Real-time Scheduler

   ```
   const struct sched_class rt_sched_class = {
   	.next			= &fair_sched_class,
   	.enqueue_task		= enqueue_task_rt,
   	.dequeue_task		= dequeue_task_rt,
   	.yield_task		= yield_task_rt,
   	.check_preempt_curr	= check_preempt_curr_rt,
   ```

4. CFS，选择vruntime值最小的进程运行。nice越大，虚拟时间过的越慢。CFS使用红黑树来组织就绪队列，因此可以最快找到vruntime值最小的那个进程，pick_next_task_fair()

   ```
   const struct sched_class fair_sched_class = {
   	.next			= &idle_sched_class,
   	.enqueue_task		= enqueue_task_fair,
   	.dequeue_task		= dequeue_task_fair,
   	.yield_task		= yield_task_fair,
   	.yield_to_task		= yield_to_task_fair,
   ```

5. Idle Scheduler，只有CPU空闲时才会运行的调度算法，它用于在系统空闲时执行后台任务。

调度类的结构体中关键元素

- enqueue_task：在运行队列中添加一个新进程
- dequeue_task：当进程从运行队列中移出时
- yield_task：当进程想自愿放弃CPU时
- pick_next_task：schedule()调用pick_next_task的函数。从它的类中挑选出下一个最佳可运行的任务。

组调度，CFS引入group scheduling，其中时间片被分配给线程组而不是单个线程。在组调度下，进程A及其创建的线程属于一个组，进程B及其创建的线程属于另一个组。

### 时间片

time slice是os用来表示进程被调度进来与被调度出去之间所能维持运行的时间长度。通常系统都有默认的时间片，但是很难确定多长的时间片是合适的，典型时长是10 milliseconds。

```
/*
 * default timeslice is 100 msecs (used only for SCHED_RR tasks).
 * Timeslices get refilled after they expire.
 */
#define RR_TIMESLICE		(100 * HZ / 1000)
```

### 调度入口

进程的调度是从调用通用调度器schedule函数开始的，schedule()功能是挑选下一个可运行任务，由pick_next_task()来遍历这些调度类选出下一个任务。该函数首先会在cfs类中寻找下一个最佳任务。

判断方式：运行队列中可运行的任务总数nr_running是否等于CFS类的子运行队列中的可运行任务的总数来判断。否则会遍历所有其它类并挑选下一个最佳可运行任务。

```
static inline struct task_struct *
pick_next_task(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	const struct sched_class *class;
	struct task_struct *p;

	/*
	 * Optimization: we know that if all tasks are in the fair class we can
	 * call that function directly, but only if the @prev task wasn't of a
	 * higher scheduling class, because otherwise those lose the
	 * opportunity to pull in more work from other CPUs.
	 */
	if (likely((prev->sched_class == &idle_sched_class ||
		    prev->sched_class == &fair_sched_class) &&
		   rq->nr_running == rq->cfs.h_nr_running)) {

		p = pick_next_task_fair(rq, prev, rf);
```

### Task状态

| TASK_RUNNING         | 等待运行状态                                                 |
| -------------------- | ------------------------------------------------------------ |
| TASK_INTERRUPTIBLE   | 可中断睡眠状态                                               |
| TASK_UNINTERRUPTIBLE | 不可中断睡眠状态                                             |
| TASK_STOPPED         | 停止状态（收到 SIGSTOP、SIGTTIN、SIGTSTP 、 SIG.TTOU信号后状态 |
| TASK_TRACED          | 被调试状态                                                   |
| TASK_KILLABLE        | 新可中断睡眠状态                                             |
| TASK_PARKED          | kthread_park使用的特殊状态                                   |
| TASK_NEW             | 创建任务临时状态                                             |
| TASK_DEAD            | 表示进程已经退出，但是还没有被父进程回收，也就是僵尸进程     |
| TASK_IDLE            | 任务空闲状态                                                 |
| TASK_WAKING          | 被唤醒状态                                                   |

## 内核Tracepoint的实现

下面代码定义了一个tracepoint函数，DEFINE_EVENT本质是封装了__DECLARE_TRACE宏，该宏定义了一个tracepoint结构对象。

```
struct tracepoint {
	const char *name;		/* Tracepoint name */
	struct static_key key;
	int (*regfunc)(void);
	void (*unregfunc)(void);
	struct tracepoint_func __rcu *funcs;
};

DEFINE_EVENT(sched_wakeup_template, sched_wakeup,
	     TP_PROTO(struct task_struct *p),
	     TP_ARGS(p));

#define DEFINE_EVENT(template, name, proto, args)		\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
	
#define DECLARE_TRACE(name, proto, args)				\
	__DECLARE_TRACE(name, PARAMS(proto), PARAMS(args),		\
			cpu_online(raw_smp_processor_id()),		\
			PARAMS(void *__data, proto),			\
			PARAMS(__data, args))
			
#define __DECLARE_TRACE(name, proto, args, cond, data_proto, data_args) \
	extern struct tracepoint __tracepoint_##name;			\
	static inline void trace_##name(proto)				\
	{
```

内核函数会调用插桩点trace_sched_waking。

```
static int
try_to_wake_up(struct task_struct *p, unsigned int state, int wake_flags)
{
	unsigned long flags;
	int cpu, success = 0;

	preempt_disable();
	if (p == current) {
		if (!(p->state & state))
			goto out;

		success = 1;
		trace_sched_waking(p);
		p->state = TASK_RUNNING;
		trace_sched_wakeup(p);
		goto out;
	}
```

## BPF对CPU的分析能力

1. 为什么CPU系统调用时间很高？是系统调用导致的吗？具体是那些系统调用？
2. 线程每次唤醒时在CPU上花费了多长时间？
3. 线程在运行队列中等待的时间有多长？
4. 当前运行队列中有多少线程在等待执行？
5. 不同CPU之间的运行队列是否均衡？
6. 那些软中断和硬中断占用了CPU时间？
7. 当其它运行队列中有需要运行的程序时，那些CPU任然处于空闲状态。

### eBPF程序的消耗

跟踪CPU调度器事件，效率非常重要，因为上下文切换这样的调度器事件每秒可能触发几百万次。如果每次上下文切换都执行eBPF程序，累积起来性能消耗也是很可观的，10%的消耗是书上说的最糟糕。    

### runqlat 

Time a task spends waiting on a runqueue for it's turn to run on the processor，我第一感觉就是enqueue_task到schedule之间的时间消耗，**很像排队上机到上机的这段时间**。

#### 内核唤醒

唤醒操作是通过函数wake_up进行，它会唤醒指定的等待队列上的所有进程。它调用函数try_to_wake_up()，该函数负责将进程设置为TASK_RUNNING状态，调用enqueue_task()将此进程放入红黑树中，如果被唤醒的进程优先级比当前正在执行的进程优先级高，还要设置need_resched标志。

need_resched标志：来表明是否需要重新执行一次调度，该标志对于内核来讲是一个信息，**它表示有其他进程应当被运行了，要尽快调用调度程序**。相关代码在kernel/sched/core.c

我们通过工具来观察下try_to_wake_up函数设置TASK_RUNNING，并插入运行队列中的流程。

通过分析代码ttwu_do_activate函数中会调用**activate_task**和**ttwu_do_wakeup**。

```
static void ttwu_do_activate(struct rq *rq, struct task_struct *p,
			     int wake_flags, struct rq_flags *rf)
{
......
	activate_task(rq, p, en_flags);
	ttwu_do_wakeup(rq, p, wake_flags, rf);
}
```

函数active_task中会将task加入运行队列

```
void activate_task(struct rq *rq, struct task_struct *p, int flags)
{
	enqueue_task(rq, p, flags);
	p->on_rq = TASK_ON_RQ_QUEUED;
}
```

函数ttwu_do_wakeup会将任务状态设置为TASK_RUNNING，同时调用插桩的tracepoint

```
static void ttwu_do_wakeup(struct rq *rq, struct task_struct *p, int wake_flags,
			   struct rq_flags *rf)
{
	check_preempt_curr(rq, p, wake_flags);
	p->state = TASK_RUNNING;
	trace_sched_wakeup(p);
```

使用perf-tool的funcgraph来观察下ttwu_do_activate函数的子调用，看看是否会调用上面的两个函数。执行命令 **./funcgraph -d 1 -HtP -m 3 ttwu_do_activate > out** ， 的确，可以看到enqueue_task_fair和ttwu_do_wakeup的调用。funcgraph用来观察函数内的子调用，这点非常方便。

```
27050.671092 |   6)    <idle>-0    |               |  ttwu_do_activate() {
27050.671092 |   6)    <idle>-0    |               |    psi_task_change() {
27050.671092 |   6)    <idle>-0    |   0.078 us    |      record_times();
27050.671093 |   6)    <idle>-0    |   0.620 us    |    }
27050.671093 |   6)    <idle>-0    |               |    enqueue_task_fair() {
27050.671093 |   6)    <idle>-0    |   0.598 us    |      enqueue_entity();
27050.671094 |   6)    <idle>-0    |   0.027 us    |      hrtick_update();
27050.671094 |   6)    <idle>-0    |   1.106 us    |    }
27050.671094 |   6)    <idle>-0    |               |    ttwu_do_wakeup() {
27050.671094 |   6)    <idle>-0    |   0.188 us    |      check_preempt_curr();
```

从代码分析唤醒的调用堆栈：**try_to_wake_up()----->ttwu_queue()----->ttwu_do_activate()**。

使用bpftrace来观察下ttw_do_activate函数的调用堆栈，这是外部调用，bpftrace -e 'kprobe:ttwu_do_activate { @[kstack] = count(); }'，看到堆栈如下

```
@[
    ttwu_do_activate+1
    try_to_wake_up+422
    hrtimer_wakeup+30
    __hrtimer_run_queues+256
    hrtimer_interrupt+256
    smp_apic_timer_interrupt+106
    apic_timer_interrupt+15
    native_safe_halt+14
    acpi_idle_do_entry+70
    acpi_idle_enter+155
    cpuidle_enter_state+135
    cpuidle_enter+44
    do_idle+564
    cpu_startup_entry+111
    start_secondary+411
    secondary_startup_64_no_verify+194
]: 526
```

感觉使用bcc的trace工具看起来更直观，trace 'ttwu_do_activate' -K -T -a

```
15:27:02 0       0       swapper/0       ttwu_do_activate 
        ffffffffa711c141 ttwu_do_activate+0x1 [kernel]
        ffffffffa711d186 try_to_wake_up+0x1a6 [kernel]
        ffffffffa717d6ee hrtimer_wakeup+0x1e [kernel]
        ffffffffa717d920 __hrtimer_run_queues+0x100 [kernel]
        ffffffffa717e0f0 hrtimer_interrupt+0x100 [kernel]
        ffffffffa7a026ba smp_apic_timer_interrupt+0x6a [kernel]
        ffffffffa7a01c4f apic_timer_interrupt+0xf [kernel]
        ffffffffa79813ae native_safe_halt+0xe [kernel]
        ffffffffa7981756 acpi_idle_do_entry+0x46 [kernel]
        ffffffffa756a18b acpi_idle_enter+0x9b [kernel]
        ffffffffa77435a7 cpuidle_enter_state+0x87 [kernel]
        ffffffffa774393c cpuidle_enter+0x2c [kernel]
        ffffffffa7122a84 do_idle+0x234 [kernel]
        ffffffffa7122c7f cpu_startup_entry+0x6f [kernel]
        ffffffffa8c19267 start_kernel+0x51d [kernel]
        ffffffffa7000107 secondary_startup_64_no_verify+0xc2 [kernel]
```

#### 唤醒抢占（Wakeup Preemption）

当一个任务被唤醒，选择一个CPU的rq插入，然后将状态设置为TASK_RUNNING后，调度器会立即进行判断CPU当前运行的任务是否被抢占，一旦调度算法决定剥夺当前任务的运行，就会设置TIF_NEED_RESCHED标志。

继续跟踪ttwu_do_wakeup，core.c文件中函数check_preempt_curr逻辑中会调用resched_curr，这个函数会设置TIF_NEED_RESCHED标志，而调度类的实现函数sched_class->check_preempt_curr也会在流程判断是否调用resched_curr函数。

```
void check_preempt_curr(struct rq *rq, struct task_struct *p, int flags)
{
	const struct sched_class *class;

	if (p->sched_class == rq->curr->sched_class) {
		rq->curr->sched_class->check_preempt_curr(rq, p, flags);
	} else {
		for_each_class(class)
		{
			if (class == rq->curr->sched_class)
				break;
			if (class == p->sched_class) {
				resched_curr(rq);
				break;
			}
		}
	}
	.......
```

我们用bcc的trace来观察下resched_curr堆栈，**trace 'resched_curr' -K -T -a**。下面堆栈符合了check_preempt_curr函数逻辑

```
15:57:58 3050    3449    PLUGINSD[ebpf]  resched_curr     
        ffffffffa711ba31 resched_curr+0x1 [kernel]
        ffffffffa712970d check_preempt_wakeup+0x18d [kernel]
        ffffffffa711bfd2 check_preempt_curr+0x62 [kernel]
        ffffffffa711c019 ttwu_do_wakeup+0x19 [kernel]
        ffffffffa711da8c sched_ttwu_pending+0xcc [kernel]
        ffffffffa711dd84 scheduler_ipi+0xa4 [kernel]
        ffffffffa7a02425 smp_reschedule_interrupt+0x45 [kernel]
        ffffffffa7a01d2f reschedule_interrupt+0xf [kernel]
        
15:57:58 0       0       swapper/2       resched_curr     
        ffffffffa711ba31 resched_curr+0x1 [kernel]
        ffffffffa711bfea check_preempt_curr+0x7a [kernel]
        ffffffffa711c019 ttwu_do_wakeup+0x19 [kernel]
        ffffffffa711d186 try_to_wake_up+0x1a6 [kernel]
        ffffffffa71d4743 watchdog_timer_fn+0x53 [kernel]
        ffffffffa717d920 __hrtimer_run_queues+0x100 [kernel]
        ffffffffa717e0f0 hrtimer_interrupt+0x100 [kernel]
        ffffffffa7a026ba smp_apic_timer_interrupt+0x6a [kernel]
        ffffffffa7a01c4f apic_timer_interrupt+0xf [kernel]
        ffffffffa79813ae native_safe_halt+0xe [kernel]
        ffffffffa7981756 acpi_idle_do_entry+0x46 [kernel]
        ffffffffa756a18b acpi_idle_enter+0x9b [kernel]
        ffffffffa77435a7 cpuidle_enter_state+0x87 [kernel]
        ffffffffa774393c cpuidle_enter+0x2c [kernel]
        ffffffffa7122a84 do_idle+0x234 [kernel]
        ffffffffa7122c7f cpu_startup_entry+0x6f [kernel]
        ffffffffa705929b start_secondary+0x19b [kernel]
        ffffffffa7000107 secondary_startup_64_no_verify+0xc2 [kernel]
```

被抢占的进程被设置TIF_NEED_RESCHED后，并没有立即调用schedule函数发生上下文切换。

#### 唤醒的tracepoint

使用的是btf raw tracepoint

- tp_btf/sched_wakeup，调用函数trace_sched_wakeup()，入口函数是try_to_wake_up/ttwu_do_wakeup。
- tp_btf/sched_wakeup_new，调用函数trace_sched_wakeup_new。入口函数是wake_up_new_task。

```
/*
 * Tracepoint called when the task is actually woken; p->state == TASK_RUNNNG.
 * It it not always called from the waking context.
 */
DEFINE_EVENT(sched_wakeup_template, sched_wakeup,
	     TP_PROTO(struct task_struct *p),
	     TP_ARGS(p));

/*
 * Tracepoint for waking up a new task:
 */
DEFINE_EVENT(sched_wakeup_template, sched_wakeup_new,
	     TP_PROTO(struct task_struct *p),
	     TP_ARGS(p));
```

#### 运行的tracepoint

tp_btf/sched_switch当一个任务被schedule选中放入CPU运行会触发该Tracepoint。在内核中，函数sched_switch()用于在进程之间上下文切换，该函数的主要功能是将当前正在运行的进程放回进程队列，并选择下一个要运行的进程。

- prev：指向正在运行的进程的task_struct结构。
- next：指向下一个要运行的进程的task_struct结构。

tracepoint定义

```
TRACE_EVENT(sched_switch,

	TP_PROTO(bool preempt,
		 struct task_struct *prev,
		 struct task_struct *next),

	TP_ARGS(preempt, prev, next),
```

使用trace命令观察sched_switch这个tracepoint，**trace 't:sched:sched_switch "prev=%s next=%s", args->prev_comm, args->next_comm' -K -T -a**

```
14:49:57 0       0       swapper/5       sched_switch     prev=swapper/5 next=sshd
        ffffffffa797bb68 __sched_text_start+0x338 [kernel]
        ffffffffa797bb68 __sched_text_start+0x338 [kernel]
        ffffffffa797c28e schedule_idle+0x1e [kernel]
        ffffffffa71229c1 do_idle+0x171 [kernel]
        ffffffffa7122c7f cpu_startup_entry+0x6f [kernel]
        ffffffffa705929b start_secondary+0x19b [kernel]
        ffffffffa7000107 secondary_startup_64_no_verify+0xc2 [kernel]
```

#### schedule的调用时机

__schedule是调度器的核心函数，作用是让调度器选择和切换到一个合适的进程并运行，调度的时机有三种

1. 阻塞操作：互斥量，信号量，等待队列。

   ```
   @[
       schedule+1
       futex_wait_queue_me+182
       futex_wait+287
       do_futex+725
       __x64_sys_futex+325
       do_syscall_64+91
       entry_SYSCALL_64_after_hwframe+101
   ]: 2196
   ```

2. 在中断返回前和系统调用返回用户空间时，检查TIF_NEED_RESCHED标志位以判断是否需要调度。

   ```
   static void exit_to_usermode_loop(struct pt_regs *regs, u32 cached_flags)
   {
   	/*
   	 * In order to return to user mode, we need to have IRQs off with
   	 * none of EXIT_TO_USERMODE_LOOP_FLAGS set.  Several of these flags
   	 * can be set at any time on preemptible kernels if we have IRQs on,
   	 * so we need to loop.  Disabling preemption wouldn't help: doing the
   	 * work to clear some of the flags can sleep.
   	 */
   	while (true) {
   		/* We have work to do. */
   		local_irq_enable();
   
   		if (cached_flags & _TIF_NEED_RESCHED)
   			schedule();
   ```

3. 将要被唤醒的进程不会马上调度schedule，而是会被添加到CFS就绪队列中，并且设置TIF_NEED_RESCHED标志位。那么被唤醒的进程什么时候被调度呢？抢占内核分为两种情况
   1. 如果唤醒动作发生在系统调用或者异常处理上下文中，那么在下一次调用preempt_enable时会检查是否需要抢占调度。
   2. 如果唤醒动作发生在硬件中断处理上下文中，那么硬件中断处理返回前会检查是否要抢占当前进程。

schedule函数还有几个变种：

- preempt_schedule
- preempt_schedule_irq
- schedule_timeout，进程睡眠到timeout指定超时时间为止。

代码：

```
#ifdef CONFIG_PREEMPTION
#define preempt_enable() \
do { \
	barrier(); \
	if (unlikely(preempt_count_dec_and_test())) \
		__preempt_schedule(); \
} while (0)
```

preempt_count_dec_and_test，prermpt_count减1后为0，且TIF_NEED_RESCHED被置位，则进行schedule()调度抢占

```
static __always_inline bool __preempt_count_dec_and_test(void)
{
	return !--*preempt_count_ptr() && tif_need_resched();
}
```

core.c中实现了preempt_schedule

```
asmlinkage __visible void __sched notrace preempt_schedule(void)
{
	/*
	 * If there is a non-zero preempt_count or interrupts are disabled,
	 * we do not want to preempt the current task. Just return..
	 */
	if (likely(!preemptible()))
		return;

	preempt_schedule_common();
}
```

preempt_schedule_common调用__schedule，执行进程调度

```
static void __sched notrace preempt_schedule_common(void)
{
	do {
		/*
		 * Because the function tracer can trace preempt_count_sub()
		 * and it also uses preempt_enable/disable_notrace(), if
		 * NEED_RESCHED is set, the preempt_enable_notrace() called
		 * by the function tracer will call this function again and
		 * cause infinite recursion.
		 *
		 * Preemption must be disabled here before the function
		 * tracer can trace. Break up preempt_disable() into two
		 * calls. One to disable preemption without fear of being
		 * traced. The other to still record the preemption latency,
		 * which can also be traced by the function tracer.
		 */
		preempt_disable_notrace();
		preempt_latency_start(1);
		__schedule(true);
		preempt_latency_stop(1);
		preempt_enable_no_resched_notrace();

		/*
		 * Check again in case we missed a preemption opportunity
		 * between schedule and now.
		 */
	} while (need_resched());
}

static __always_inline bool need_resched(void)
{
	return unlikely(tif_need_resched());
}
```

#### cond_resched宏的作用

```
#ifndef CONFIG_PREEMPTION
int __sched _cond_resched(void)
{
	if (should_resched(0)) {
		preempt_schedule_common();
		return 1;
	}
	rcu_all_qs();
	return 0;
}
EXPORT_SYMBOL(_cond_resched);
#endif

#define cond_resched()                                                         \
	({                                                                     \
		___might_sleep(__FILE__, __LINE__, 0);                         \
		_cond_resched();                                               \
	})
```

该段代码是一个宏定义，它定义了一个名为`cond_resched()`的宏。宏的作用是在某些情况下允许内核调度器进行调度切换。这个宏在 Linux 内核中使用，用于确保在一些长时间运行的内核代码路径中，其他等待运行的任务也有机会得到执行，以避免系统出现长时间的“死锁”或“饥饿”状态。

1. `___might_sleep(__FILE__, __LINE__, 0);`

   `___might_sleep()` 是一个内核函数，用于检查当前上下文是否是可以睡眠的（sleepable context）。在 Linux 内核中，有一些上下文是不允许进入睡眠状态的，例如中断处理程序和一些原子上下文。如果在不允许睡眠的上下文中调用了可能导致当前任务睡眠的代码，那么可能会导致严重的问题。因此，`___might_sleep()` 用于检查并在出现问题时打印警告信息。`__FILE__` 和 `__LINE__` 是 C 预处理器内置的宏，用于获取当前文件名和行号，以便在警告信息中指示问题发生的位置。

2. `_cond_resched();`

   `_cond_resched()` 是另一个内核函数，它实际上用于检查是否需要进行调度切换。如果当前任务运行时间过长，内核会在适当的时机调用 `_cond_resched()`，从而允许其他等待运行的任务得到执行。这样可以防止长时间运行的任务占用 CPU 资源过多，导致其他任务无法得到运行的情况。

3. `({ ___might_sleep(__FILE__, __LINE__, 0); _cond_resched(); })`

   这是一个 C 语言的代码块，其中首先调用 `___might_sleep()` 进行睡眠上下文检查，然后调用 `_cond_resched()` 进行调度切换。最终，这个代码块作为整体在调用时会被替换为这两个函数的执行。

总结起来，`cond_resched()` 宏的作用是在一些内核代码中，允许调度器进行调度切换，以确保其他等待运行的任务也有机会执行，从而避免系统出现长时间的“死锁”或“饥饿”状态。在调用该宏之前，会通过 `___might_sleep()` 检查当前上下文是否是允许睡眠的上下文，以避免在不允许睡眠的上下文中调用可能导致当前任务睡眠的代码。

## 资料

1. [【原创】Kernel调试追踪技术之 Ftrace on ARM64 - HPYU - 博客园 (cnblogs.com)](https://www.cnblogs.com/hpyu/p/14348523.html)
2. https://mozillazg.com/2022/06/ebpf-libbpf-btf-powered-enabled-raw-tracepoint-common-questions.html#hidsec
3. [Linux 内核静态追踪技术的实现 - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/433010401)
4. [「Let's Go eBPF」认识数据源：Tracepoint | Serica (iserica.com)](https://www.iserica.com/posts/brief-intro-for-tracepoint/)
5. [linux 任务状态定义 – GarlicSpace](https://garlicspace.com/2019/06/29/linux任务状态定义/)
6. [CFS调度器（2）-源码解析 (wowotech.net)](http://www.wowotech.net/process_management/448.html)
7. [Linux Preemption - 2 | Oliver Yang](http://oliveryang.net/2016/03/linux-scheduler-2/)
8. [一文让你明白CPU上下文切换 - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/52845869)
9. [深入理解Linux内核之进程唤醒 - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/402374191)  代码注释

