1. hung task 检测设置

   ```
    ⚡ root@localhost  ~  sysctl -a --pattern hung
   kernel.hung_task_check_count = 4194304
   kernel.hung_task_panic = 0
   kernel.hung_task_timeout_secs = 120
   kernel.hung_task_warnings = 10
   ```

2. 编译

   ```
   make CFLAGS_EXTRA="-ggdb -DDEBUG" modules_install
   ```

3. 运行 hung task，可见状态为 D，信号 9 无法 kill

   ```
   ⚡ root@192  /lib/modules/4.18.0-348.7.1.cw.x86_64/extra/calmwu_modules/cw_hungtask_test  ps -eo stat,comm,pid,lstart,etime|grep hung
   S    khungtaskd           70 Sat Oct 19 17:14:05 2024       45:24
   D    hung_task_threa    8765 Sat Oct 19 17:57:43 2024       01:46
   D    hung_task_threa    8768 Sat Oct 19 17:57:43 2024       01:46
   D    hung_task_threa    8769 Sat Oct 19 17:57:43 2024       01:46
   D    hung_task_threa    8770 Sat Oct 19 17:57:43 2024       01:46
   D    hung_task_threa    8771 Sat Oct 19 17:57:43 2024       01:46
   
   ```

4. 线程函数退出后，module exit没有判断而调用kthread_stop导致crash

   - crash堆栈，**rip：ffffffff8df1078a，kthread_stop+42**

     ```
     crash> bt
     PID: 8417     TASK: ffff9c462243c800  CPU: 5    COMMAND: "rmmod"
      #0 [ffffb74fc5507bd8] machine_kexec at ffffffff8de6429e
      #1 [ffffb74fc5507c30] __crash_kexec at ffffffff8df9f83d
      #2 [ffffb74fc5507cf8] crash_kexec at ffffffff8dfa072d
      #3 [ffffb74fc5507d10] oops_end at ffffffff8de2619d
      #4 [ffffb74fc5507d30] no_context at ffffffff8de7561f
      #5 [ffffb74fc5507d88] __bad_area_nosemaphore at ffffffff8de7597c
      #6 [ffffb74fc5507dd0] do_page_fault at ffffffff8de76277
      #7 [ffffb74fc5507e00] page_fault at ffffffff8e80111e
         [exception RIP: kthread_stop+42]
         RIP: ffffffff8df1078a  RSP: ffffb74fc5507eb8  RFLAGS: 00010246
         RAX: ffffffffc089d1c4  RBX: ffff9c46410a8000  RCX: 0000000000000000
         RDX: ffff9c462243c800  RSI: ffffb74fc5507ee8  RDI: ffff9c46410a8000
         RBP: 00007f9b8f4f9a10   R8: 0000000000000000   R9: 0000000000000074
         R10: 0000000000000030  R11: 0000000000000000  R12: 0000000000000000
         R13: 0000000000000000  R14: 0000000000000000  R15: 0000000000000000
         ORIG_RAX: ffffffffffffffff  CS: 0010  SS: 0018
      #8 [ffffb74fc5507ec8] cw_hungtask_test_exit at ffffffffc089d1e1 [cw_hungtask_test]
      #9 [ffffb74fc5507ee0] __x64_sys_delete_module at ffffffff8df99fe9
     #10 [ffffb74fc5507f38] do_syscall_64 at ffffffff8de042bb
     #11 [ffffb74fc5507f50] entry_SYSCALL_64_after_hwframe at ffffffff8e8000ad
         RIP: 00007faafe4b814b  RSP: 00007fff0f22abf8  RFLAGS: 00000206
         RAX: ffffffffffffffda  RBX: 0000559464cb87b0  RCX: 00007faafe4b814b
         RDX: 000000000000000a  RSI: 0000000000000800  RDI: 0000559464cb8818
         RBP: 0000000000000000   R8: 00007fff0f229b71   R9: 0000000000000000
         R10: 00007faafe5ef9a0  R11: 0000000000000206  R12: 00007fff0f22ae10
         R13: 00007fff0f22b461  R14: 0000559464cb82a0  R15: 0000559464cb87b0
         ORIG_RAX: 00000000000000b0  CS: 0033  SS: 002b
     ```

   - 加载内核模块

     ```
     gdb: gdb request failed: l kthread_stop+42
     crash> mod -s cw_hungtask_test /home/calmwu/Program/x-monitor/examples/kernel_dev/hungtask_test/cw_hungtask_test.ko
          MODULE       NAME                                     BASE           SIZE  OBJECT FILE
     ffffffffc089f0c0  cw_hungtask_test                   ffffffffc089d000    16384  /home/calmwu/Program/x-monitor/examples/kernel_dev/hungtask_test/cw_hungtask_test.ko
     ```

   - 查看rip对应的代码

     ```
     crash> dis -l kthread_stop+42 5
     crash> dis kthread_stop
     0xffffffff8df10760 <kthread_stop>:      nopl   0x0(%rax,%rax,1) [FTRACE NOP]
     0xffffffff8df10765 <kthread_stop+5>:    push   %rbp
     0xffffffff8df10766 <kthread_stop+6>:    push   %rbx
     0xffffffff8df10767 <kthread_stop+7>:    mov    %rdi,%rbx
     0xffffffff8df1076a <kthread_stop+10>:   nopl   0x0(%rax,%rax,1)
     0xffffffff8df1076f <kthread_stop+15>:   lock incl 0x20(%rbx)
     0xffffffff8df10773 <kthread_stop+19>:   js     0xffffffff8e772a4d <__noinstr_text_end+240>
     0xffffffff8df10779 <kthread_stop+25>:   testb  $0x20,0x26(%rbx)
     0xffffffff8df1077d <kthread_stop+29>:   je     0xffffffff8df10858 <kthread_stop+248>
     0xffffffff8df10783 <kthread_stop+35>:   mov    0x9d8(%rbx),%rbp
     
     /home/calmwu/Program/kernel/linux-4.18.0-348.7.1.el8_5/./arch/x86/include/asm/bitops.h: 76
     0xffffffff8df1078a <kthread_stop+42>:   lock orb $0x2,0x0(%rbp)
     /home/calmwu/Program/kernel/linux-4.18.0-348.7.1.el8_5/kernel/kthread.c: 666
     0xffffffff8df1078f <kthread_stop+47>:   mov    %rbx,%rdi
     0xffffffff8df10792 <kthread_stop+50>:   call   0xffffffff8df106f0 <kthread_unpark>
     /home/calmwu/Program/kernel/linux-4.18.0-348.7.1.el8_5/kernel/kthread.c: 667
     0xffffffff8df10797 <kthread_stop+55>:   mov    %rbx,%rdi
     0xffffffff8df1079a <kthread_stop+58>:   call   0xffffffff8df1d7d0 <wake_up_process>
     ```

   - 应该和rbp寄存器有关

     ```
     crash> p __hung_tasks
     __hung_tasks = $1 = 
      {0xffff9c46410a8000, 0xffff9c46410ae000, 0xffff9c45c5ae4800, 0xffff9c45cc118000, 0xffff9c45ccac3000}
     ```

     
