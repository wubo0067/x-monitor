1. 运行

   ```
   stap -g -s 16 -v ./list_kmod_info.stp
   ```

2. 获取的全局变量和/proc/kallsyms文件中一样

   ```
   modules: 0xffffffff936359f0
   
    ✘ ⚡ root@localhost  /home/calmwu/Program/x-monitor/tools/stap/kmod  grep ffffffff936359f0 /proc/kallsyms 
   ffffffff936359f0 d modules
   ```

3. 使用guru模式才能输出printk到dmesg

   ```
   // !!要调用printk输出到dmesg，需要使用guru模式
   function list_kmods:long(modules:long) %{  /* guru */
   ```

4. kmod的引用计数和depend on me不一致的

   ```
   [Wed Oct 30 18:12:56 2024] kmod name:'nft_reject_inet', refcnt:5
   [Wed Oct 30 18:12:56 2024] depend on me <===
   [Wed Oct 30 18:12:56 2024] i depend on ===>
   [Wed Oct 30 18:12:56 2024] 	nf_reject_ipv4
   [Wed Oct 30 18:12:56 2024] 	nf_tables
   [Wed Oct 30 18:12:56 2024] 	nf_reject_ipv6
   [Wed Oct 30 18:12:56 2024] 	nft_reject
   ```

   ```
    ⚡ root@localhost  /usr/share  lsmod |grep nft_reject_inet
   nft_reject_inet        16384  4
   ```

5. 资料

   内核模块的引用计数：[Linux Kernel Modules: When to use try_module_get / module_put - Stack Overflow](https://stackoverflow.com/questions/1741415/linux-kernel-modules-when-to-use-try-module-get-module-put)

   