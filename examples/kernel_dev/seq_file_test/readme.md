1. 查看seq_file输出

   ```
    ⚡ root@localhost  ~   cat /proc/seq_num
   Start
   0
   1
   2
   3
   End
   ```

2. dmesg -T输出😊

   ```
   [Sun Aug 11 19:21:11 2024] seq_file_test:cw_seqfile_test_init():164: Module:[seqfile_test] init successfully!
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_start():35: Module:[seqfile_test] seq iteration started--->
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_show():59: Module:[seqfile_test] seq iteration show:(0)
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_next():74: Module:[seqfile_test] seq iteration next:(1)
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_show():59: Module:[seqfile_test] seq iteration show:(1)
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_next():74: Module:[seqfile_test] seq iteration next:(2)
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_show():59: Module:[seqfile_test] seq iteration show:(2)
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_next():74: Module:[seqfile_test] seq iteration next:(3)
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_show():59: Module:[seqfile_test] seq iteration show:(3)
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_next():74: Module:[seqfile_test] seq iteration next:(4)
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_stop():84: Module:[seqfile_test] seq iteration stop<---
   [Sun Aug 11 19:21:16 2024] seq_file_test:num_seq_start():46: Module:[seqfile_test] seq iteration reached the end
   ```

3. 什么时候结束迭代

   在seq_operations.start和seq_operations.next函数都返回NULL时，seq_file会结束迭代。