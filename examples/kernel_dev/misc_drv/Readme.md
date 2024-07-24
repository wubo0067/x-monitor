# Readme

1. crash定位

   ```
   [56433.608947] BUG: unable to handle kernel NULL pointer dereference at 0000000000000008
   [56433.610044] PGD 0 P4D 0 
   [56433.610549] Oops: 0000 [#1] SMP PTI
   [56433.611040] CPU: 5 PID: 82778 Comm: cat Kdump: loaded Tainted: G           OE    --------- -  - 4.18.0-425.3.1.el8.x86_64 #1
   [56433.612039] Hardware name: VMware, Inc. VMware Virtual Platform/440BX Desktop Reference Platform, BIOS 6.00 11/12/2020
   [56433.613016] RIP: 0010:d_path+0x8c/0x140
   [56433.613735] Code: 00 00 00 48 83 c4 28 5b 5d e9 f0 96 86 00 48 8b 0b 48 3b 39 75 d4 65 48 8b 04 25 40 5c 01 00 48 8b 90 18 0b 00 00 eb 02 f3 90 <8b> 42 08 a8 01 75 f7 48 8b 72 18 48 8b 7a 20 48 89 74 24 10 48 89
   [56433.615139] RSP: 0018:ffffbcaf423bbc98 EFLAGS: 00010246
   [56433.615642] RAX: ffffa012c0e48000 RBX: ffffa012606c3610 RCX: 0000000000000000
   [56433.616139] RDX: 0000000000000000 RSI: ffffa0120f3fb000 RDI: ffffa0131366b600
   [56433.616634] RBP: ffffbcaf423bbcc8 R08: ffffbcaf423bbc90 R09: ffffa0120f3fb000
   [56433.617126] R10: ffffbcaf423bbcf0 R11: ffffa012606c3610 R12: 00000000000a0001
   [56433.617598] R13: ffffa01246f9eae8 R14: ffffa012104f79a0 R15: ffffa0131366b600
   [56433.618089] FS:  0000000000000000(0000) GS:ffffa0152fd40000(0000) knlGS:0000000000000000
   [56433.618560] CS:  0010 DS: 0000 ES: 0000 CR0: 0000000080050033
   [56433.619033] CR2: 0000000000000008 CR3: 0000000038410006 CR4: 0000000000370ee0
   [56433.619529] Call Trace:
   [56433.620008]  __release_cw_miscdev+0x3d/0x70 [misc_drv_test]
   [56433.620846]  __fput+0xbe/0x250
   [56433.621593]  task_work_run+0x8a/0xb0
   [56433.622495]  do_exit+0x34d/0xb10
   [56433.623183]  ? tty_insert_flip_string_fixed_flag+0x85/0xe0
   [56433.624467]  do_group_exit+0x3a/0xa0
   [56433.624598]  get_signal+0x158/0x870
   [56433.624738]  do_signal+0x36/0x690
   [56433.625445]  ? vfs_write+0x17c/0x1b0
   [56433.626497]  ? vfs_write+0x17c/0x1b0
   [56433.626763]  exit_to_usermode_loop+0x89/0x100
   [56433.627353]  do_syscall_64+0x19c/0x1b0
   [56433.627612]  entry_SYSCALL_64_after_hwframe+0x61/0xc6
   [56433.628414] RIP: 0033:0x7f1397f7e628
   ```

   根据RIP找到出问题的代码，misc_drv_test.c:67

   ```
    ⚡ root@localhost  /var/crash/127.0.0.1-2024-07-17-11:25:23  //home/calmwu/Program/x-monitor/tools/bin/faddr2line /home/calmwu/Program/x-monitor/examples/kernel_dev/misc_drv/misc
   _drv_test.ko __release_cw_miscdev+0x3d
   __release_cw_miscdev+0x3d/0x70:
   __release_cw_miscdev at /home/calmwu/Program/x-monitor/examples/kernel_dev/misc_drv/misc_drv_test.c:67
   ```

2. 测试，读取misc设备

   ```
    ⚡ root@localhost  /var/crash  dd if=/dev/cw_miscdev of=readtest bs=4k count=1
   1+0 records in
   1+0 records out
   4096 bytes (4.1 kB, 4.0 KiB) copied, 0.0002486 s, 16.5 MB/s
   
   [Wed Jul 17 15:04:39 2024] misc_drv_test:__open_cw_miscdev():38: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 17 15:04:39 2024] misc_drv_test:__open_cw_miscdev():38: 002)  dd :20481   |  ...0   /* __open_cw_miscdev() */
   [Wed Jul 17 15:04:39 2024] misc_drv_test:__open_cw_miscdev():41: Module:[cw_misc_drv] opening "/dev/cw_miscdev" now; wrt open file: f_flags = 0x8000
   [Wed Jul 17 15:04:39 2024] misc_drv_test:__read_cw_miscdev():50: Module:[cw_misc_drv]to read 4096 bytes
   [Wed Jul 17 15:04:39 2024] misc_drv_test:__release_cw_miscdev():67: Module:[cw_misc_drv]closing "/dev/cw_miscdev"
   
    ⚡ root@localhost  /var/crash  hexdump readtest 
   0000000 0000 0000 0000 0000 0000 0000 0000 0000
   *
   0001000
   ```

3. 测试，写数据到misc设备

   ```
    ⚡ root@localhost  /var/crash  dmesg -C; dd if=/dev/urandom of=/dev/cw_miscdev bs=4k count=1; dmesg
   1+0 records in
   1+0 records out
   4096 bytes (4.1 kB, 4.0 KiB) copied, 0.0002668 s, 15.4 MB/s
   [13449.329242] misc_drv_test:__open_cw_miscdev():38: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [13449.329262] misc_drv_test:__open_cw_miscdev():38: 000)  dd :20591   |  ...0   /* __open_cw_miscdev() */
   [13449.329280] misc_drv_test:__open_cw_miscdev():41: Module:[cw_misc_drv] opening "/dev/cw_miscdev" now; wrt open file: f_flags = 0x8241
   [13449.329405] misc_drv_test:__write_cw_miscdev():57: Module:[cw_misc_drv]to write 4096 bytes
   [13449.329427] misc_drv_test:__release_cw_miscdev():67: Module:[cw_misc_drv]closing "/dev/cw_miscdev"
   ```

4. 测试strscpy函数

   ```
    ⚡ root@localhost  ~  dmesg -C;cat /dev/cw_miscdev;dmesg -T                    
   1234567890abcde[Wed Jul 24 18:43:29 2024] misc_drv_test:__open_cw_miscdev():44: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:43:29 2024] misc_drv_test:__open_cw_miscdev():44: 004)  cat :10837   |  ...0   /* __open_cw_miscdev() */
   [Wed Jul 24 18:43:29 2024] misc_drv_test:__open_cw_miscdev():47: Module:[cw_misc_drv] opening "/dev/cw_miscdev" now; wrt open file: f_flags = 0x8000
   [Wed Jul 24 18:43:29 2024] misc_drv_test:__read_cw_miscdev():65: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:43:29 2024] misc_drv_test:__read_cw_miscdev():65: 004)  cat :10837   |  ...0   /* __read_cw_miscdev() */
   [Wed Jul 24 18:43:29 2024] misc cw_miscdev: Module:[cw_misc_drv] 'cat' want to read 131072 bytes, offset:0
   [Wed Jul 24 18:43:29 2024] misc cw_miscdev: Module:[cw_misc_drv] read 15 bytes
   [Wed Jul 24 18:43:29 2024] misc_drv_test:__read_cw_miscdev():65: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:43:29 2024] misc_drv_test:__read_cw_miscdev():65: 004)  cat :10837   |  ...0   /* __read_cw_miscdev() */
   [Wed Jul 24 18:43:29 2024] misc cw_miscdev: Module:[cw_misc_drv] 'cat' want to read 131072 bytes, offset:15
   [Wed Jul 24 18:43:29 2024] misc cw_miscdev: Module:[cw_misc_drv] read offset:15 >= secret_len:15
   [Wed Jul 24 18:43:29 2024] misc_drv_test:__release_cw_miscdev():153: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:43:29 2024] misc_drv_test:__release_cw_miscdev():153: 004)  cat :10837   |  ...0   /* __release_cw_miscdev() */
    ⚡ root@localhost  ~  dmesg -C;echo 1234567890abcd > /dev/cw_miscdev;dmesg -T 
   [Wed Jul 24 18:45:22 2024] misc_drv_test:__open_cw_miscdev():44: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:45:22 2024] misc_drv_test:__open_cw_miscdev():44: 006)  zsh :7422   |  ...0   /* __open_cw_miscdev() */
   [Wed Jul 24 18:45:22 2024] misc_drv_test:__open_cw_miscdev():47: Module:[cw_misc_drv] opening "/dev/cw_miscdev" now; wrt open file: f_flags = 0x8341
   [Wed Jul 24 18:45:22 2024] misc_drv_test:__write_cw_miscdev():111: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:45:22 2024] misc_drv_test:__write_cw_miscdev():111: 006)  zsh :7422   |  ...0   /* __write_cw_miscdev() */
   [Wed Jul 24 18:45:22 2024] misc cw_miscdev: Module:[cw_misc_drv] zsh to write 15 bytes
   [Wed Jul 24 18:45:22 2024] Module:[cw_misc_drv]User buffer: 00000000: 31 32 33 34 35 36 37 38 39 30 61 62 63 64 0a     1234567890abcd.
   [Wed Jul 24 18:45:22 2024] Module:[cw_misc_drv]secret_buf: 00000000: 31 32 33 34 35 36 37 38 39 30 61 62 63 64 0a 00  1234567890abcd..
   [Wed Jul 24 18:45:22 2024] misc cw_miscdev: Module:[cw_misc_drv] written 15 bytes, ret:15
   [Wed Jul 24 18:45:22 2024] misc_drv_test:__release_cw_miscdev():153: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:45:22 2024] misc_drv_test:__release_cw_miscdev():153: 006)  zsh :7422   |  ...0   /* __release_cw_miscdev() */
    ⚡ root@localhost  ~  dmesg -C;cat /dev/cw_miscdev;dmesg -T                  
   1234567890abcd
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__open_cw_miscdev():44: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__open_cw_miscdev():44: 001)  cat :10877   |  ...0   /* __open_cw_miscdev() */
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__open_cw_miscdev():47: Module:[cw_misc_drv] opening "/dev/cw_miscdev" now; wrt open file: f_flags = 0x8000
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__read_cw_miscdev():65: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__read_cw_miscdev():65: 001)  cat :10877   |  ...0   /* __read_cw_miscdev() */
   [Wed Jul 24 18:45:26 2024] misc cw_miscdev: Module:[cw_misc_drv] 'cat' want to read 131072 bytes, offset:0
   [Wed Jul 24 18:45:26 2024] misc cw_miscdev: Module:[cw_misc_drv] read 15 bytes
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__read_cw_miscdev():65: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__read_cw_miscdev():65: 001)  cat :10877   |  ...0   /* __read_cw_miscdev() */
   [Wed Jul 24 18:45:26 2024] misc cw_miscdev: Module:[cw_misc_drv] 'cat' want to read 131072 bytes, offset:15
   [Wed Jul 24 18:45:26 2024] misc cw_miscdev: Module:[cw_misc_drv] read offset:15 >= secret_len:15
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__release_cw_miscdev():153: CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */
   [Wed Jul 24 18:45:26 2024] misc_drv_test:__release_cw_miscdev():153: 001)  cat :10877   |  ...0   /* __release_cw_miscdev() */
   
   ```

   