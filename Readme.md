1. ##### x-monitor

   - 依赖

     ```
     dnf install libev.x86_64 libev-devel.x86_64 libuuid-devel.x86_64
     dnf -y install elfutils-libelf-devel-static.x86_64
     
     wget https://ftp.gnu.org/gnu/nettle/nettle-3.7.tar.gz
     wget https://ftp.gnu.org/gnu/libidn/libidn2-2.3.2.tar.gz
     git clone https://github.com/libffi/libffi.git
     wget https://ftp.gnu.org/gnu/libtasn1/libtasn1-4.18.0.tar.gz
     wget https://ftp.gnu.org/gnu/libunistring/libunistring-1.0.tar.gz
     wget https://github.com/p11-glue/p11-kit/archive/refs/tags/0.24.0.tar.gz
     
     ./configure --prefix=/usr --enable-static #编译静态库
     https://www.gnutls.org/download.html
     https://www.gnu.org/software/libunistring/#TOCdownloading
     ```

   - 安装静态库，配置repo，vim CodeReady.repo

     ```
     [CodeReady]
     name=codeready
     baseurl=http://yum.oracle.com/repo/OracleLinux/OL8/codeready/builder/x86_64
     gpgcheck=0
     enabled=1
     ```

     安装静态库， yum install glibc-static libstdc++-static.x86_64 zlib-static.x86_64

   - microhttpd不支持https，减少库的依赖

     ```
     https://ftp.gnu.org/gnu/libmicrohttpd/
     ./configure --disable-https --prefix=/usr --enable-static
     ```

   - 编译

     ```
     cmake3 ../ -DCMAKE_BUILD_TYPE=Debug -DSTATIC_LINKING=1 -DSTATIC_LIBC=1
     make x-monitor VERBOSE=1
     ```
     
   - 运行

     ```
     bin/x-monitor -c ../env/config/x-monitor.cfg
     ```

   - 停止

     ```
     kill -15 `pidof x-monitor`
     ```

   - 查看状态

     ```
     top -d 1 -p `pidof x-monitor`
     pidstat -r -u -t -p  `pidof x-monitor` 1 10000
     ```

   - 代码统计

     ```
     find . -path ./extra -prune -o  -name "*.[ch]"|xargs wc -l
     ```

2. ##### proc_file

   - 编译

     ```
     make procfile_cli VERBOSE=1
     ```

   - 运行

     ```
     bin/procfile_cli ../cli/procfile_cli/log.cfg /proc/diskstats 10
     bin/procfile_cli ../cli/procfile_cli/log.cfg /proc/meminfo 10
     ```

3. ##### perf_event_stack

   - 编译

     ```
     make perf_event_stack_cli VERBOSE=1
     ```

   - 运行

4. ##### proto_statistics_cli

   - 编译

     ```
     make proto_statistics_cli VERBOSE=1
     ```

   - 查看map数据

     ```
     bpftool map dump name proto_countmap
     ```

   - 运行

     ```
     bin/proto_statistics_cli ../collectors/ebpf/kernel/xmbpf_proto_statistics_kern.5.12.o eth0
     ```

5. ##### simplepattern_test

   - 编译

     ```
     make simplepattern_test VERBOSE=1
     ```

   - 运行

     ```
     bin/simplepattern_test ../cli/simplepattern_test/log.cfg
     ```

   - 检查是否有内存泄露

     ```
     valgrind --tool=memcheck --leak-check=full bin/simplepattern_test ../cli/simplepattern_test/log.cfg
     ```

6. ##### x-monitor的性能分析

   1. 整个系统的cpu实时开销排序

      ```none
      perf top --sort cpu
      ```

   2. 进程采样

      ```
      perf record -F 99 -p 62275 -e cpu-clock -ag --call-graph dwarf sleep 10
      ```

      -F 99：每秒采样的99次

      -g：记录调用堆栈

   3. 采样结果

      ```
      perf report -n
      ```

      生成报告预览

      ```
      perf report -n --stdio
      ```

      生成详细的报告

      ```
      perf script > out.perf
      ```

      dump出perf.data的内容

   4. 生成svg图

      ```
      yum -y install perl-open.noarch
      perf script -i perf.data &> perf.unfold
      stackcollapse-perf.pl perf.unfold &> perf.folded
      flamegraph.pl perf.folded > perf.svg
      ```

7. ##### 监控指标

   1. 在Prometheus中查看指标的秒级数据

      ```
      loadavg_5min{load5="load5"}[5m]
      {__name__=~"loadavg_15min|loadavg_1min|loadavg_5min"}
      {host="localhost.localdomain:8000"}
      {meminfo!=""} 查看所有meminfo标签指标
      {psi!=""} 查看所有psi指标
      ```

      时间戳转换工具：[Unix时间戳(Unix timestamp)转换工具 - 时间戳转换工具 (bmcx.com)](https://unixtime.bmcx.com/)

   2. 直接查看x-monitor导出的指标

      ```
      curl 0.0.0.0:8000/metrics
      ```

   3. 启动Prometheus

      ```
      ./prometheus --log.level=debug
      ```

8. ##### 开启系统PSI

   通常使用的load average有几个缺点

   - load average的计算包含了TASK_RUNNING和TASK_UNINTERRUPTIBLE两种状态的进程，TASK_RUNNING是进程处于运行，或等待分配CPU的准备运行状态，TASK_UNINTERRUPTIBLE是进程处于不可中断的等待，一般是等待磁盘的输入输出。因此load average的飙高可能是因为CPU资源不够，让很多TASK_RUNNING状态的进程等待CPU，也可能是由于磁盘IO资源紧张，造成很多进程因为等待IO而处于TASK_UNINTERRUPTIBLE状态。可以通过load average发现系统很忙，但是无法区分是因为争夺CPU还是IO引起的。
   - load average最短的时间窗口是1分钟。
   - load average报告的是活跃进程的原始数据，还需要知道可用CPU核数，这样load average的值才有意义。

   PSI概览：

   ​		当 CPU、内存或 IO 设备争夺激烈的时候，系统会出现负载的延迟峰值、吞吐量下降，并可能触发内核的 `OOM Killer`。**PSI(Pressure Stall Information)** 字面意思就是由于资源（CPU、内存和 IO）压力造成的任务执行停顿。**PSI** 量化了由于硬件资源紧张造成的任务执行中断，统计了系统中任务等待硬件资源的时间。我们可以用 **PSI** 作为指标，来衡量硬件资源的压力情况。停顿的时间越长，说明资源面临的压力越大。

   ​		PSI已经包含在4.20及以上版本内核中。

   ​		[使用PSI（Pressure Stall Information）监控服务器资源 - InfoQ 写作平台](https://xie.infoq.cn/article/931eee27dabb0de906869ba05)

   开启PSI：

   ​		查看所有内核启动，grubby --info=ALL

   ​		增加内核启动参数：grubby --update-kernel=/boot/vmlinuz-4.18.0 **--args=psi=1**，重启系统。

   ​		查看PSI结果：

   ```
   		tail /proc/pressure/*
   ​		==> /proc/pressure/cpu <==
   ​		some avg10=0.00 avg60=0.55 avg300=0.27 total=1192936
   ​		==> /proc/pressure/io <==
   ​		some avg10=0.00 avg60=0.13 avg300=0.06 total=325847
   ​		full avg10=0.00 avg60=0.03 avg300=0.01 total=134192
   ​		==> /proc/pressure/memory <==
   ​		some avg10=0.00 avg60=0.00 avg300=0.00 total=0
   ​		full avg10=0.00 avg60=0.00 avg300=0.00 total=0
   ```

   



