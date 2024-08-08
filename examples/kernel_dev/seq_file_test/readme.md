1. 查看seq_file输出

   ```
    ⚡ root@localhost  ~   cat /proc/seq_task
   0
   1
   2
   .....
   8
   9
   10
   End
   ```

2. 什么时候结束迭代

   在seq_operations.start和seq_operations.next函数都返回NULL时，seq_file会结束迭代。