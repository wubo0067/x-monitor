1.  #### x-monitor

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

    - 安装静态库，配置 repo，vim CodeReady.repo
      ```
      [CodeReady]
      name=codeready
      baseurl=http://yum.oracle.com/repo/OracleLinux/OL8/codeready/builder/x86_64
      gpgcheck=0
      enabled=1
      ```
      安装静态库， yum install glibc-static libstdc++-static.x86_64 zlib-static.x86_64
    - microhttpd 不支持 https，减少库的依赖
      ```
      https://ftp.gnu.org/gnu/libmicrohttpd/
      ./configure --disable-https --prefix=/usr --enable-static
      ```
    - 编译
      - 编译 ebpf 模块，会在../user 目录生成%.skel.h 文件，同时安装 libbpf 头文件，库到开发环境。
        ```
        cd collectors/ebpf/bpf
        make V=1
        ```
      - 编译 x-montior
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

2.  #### 工具以及测试程序

    - ##### proc_file

      - 编译
      ```
      make procfile_cli VERBOSE=1
      ```

      - 运行
      ```
      bin/procfile_cli ../cli/procfile_cli/log.cfg /proc/diskstats 10
      bin/procfile_cli ../cli/procfile_cli/log.cfg /proc/meminfo 10
      ```

    - ##### perf_event_stack

      - 编译

      ```
      make perf_event_stack_cli VERBOSE=1
      ```

    - ##### proto_statistics_cli

      - 编译

        ```
         make proto_statistics_cli VERBOSE=1
        ```

      - 查看 map 数据

        ```
         bpftool map dump name proto_countmap
        ```

      - 运行
        ```
         bin/proto_statistics_cli ../collectors/ebpf/kernel/xmbpf_proto_statistics_kern.5.12.o eth0
        ```

    - ##### simplepattern_test

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

    - ##### xdp_libbpf_test

      - 编译

        - 编译生成 bpf skel、libbpf，同时安装。进入目录 x-monitor/collectors/ebpf/bpf，执行 make V=1
        - 编译用户态程序，进入目录 x-monitor/build，执行 make xdp_libbpf_test VERBOSE=1

      - 运行

        ```
        bin/xdp_libbpf_test --itf=ens160 -v -s
        ```

      - 卸载网卡 xdp

        ```
        ip link set dev ens160 xdpgeneric off
        ```

      - 问题

        - 使用 BPF_MAP_TYPE_PERCPU_ARRAY，用户态查询 map 元素时报错，bpf_map_lookup_elem failed key:0xEE (ret:-14): Bad address，value 是一个 cpu 数量的数组，根据内核源码，数组需要对齐。

          ```
          #define __bpf_percpu_val_align __attribute__((__aligned__(8)))
          #define BPF_DECLARE_PERCPU(type, name) \
          struct {              \
             type v; /* padding */      \
          } __bpf_percpu_val_align name[xm_bpf_num_possible_cpus()]
          
          #define bpf_percpu(name, cpu) name[(cpu)].v
          ```

        使用 BPF_DECLARE_PERCPU 宏来定义数组，该问题解决。value 的地址被分配到按 8 字节对齐的内存地址上。[**attribute**((**aligned**(n)))对结构体对齐的影响\_lzc285115059 的博客-CSDN 博客**\_attribute**((**aligned**(8)))](https://blog.csdn.net/lzc285115059/article/details/84454497)

3.  #### x-monitor 的性能分析

    - 整个系统的 cpu 实时开销排序

       ```none
       perf top --sort cpu
       ```

    - 进程采样

       ```
       perf record -F 99 -p 62275 -e cpu-clock -ag --call-graph dwarf sleep 10
       ```

       -F 99：每秒采样的 99 次

       -g：记录调用堆栈

    - 采样结果

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

       dump 出 perf.data 的内容

    - 生成 svg 图
       ```
       yum -y install perl-open.noarch
       perf script -i perf.data &> perf.unfold
       stackcollapse-perf.pl perf.unfold &> perf.folded
       flamegraph.pl perf.folded > perf.svg
       ```

4.  #### 监控指标

    1. 配置 Prometheus，在 prometheus.yml 文件中配置

       ```
       job_name: 'x-monitor-data'
       scrape_interval: 1s
       static_configs:
         - targets: ['0.0.0.0:8000']
       ```

    2. 在 Prometheus 中查看指标的秒级数据

       ```
       loadavg_5min{load5="load5"}[5m]
       {__name__=~"loadavg_15min|loadavg_1min|loadavg_5min"}
       {host="localhost.localdomain:8000"}
       {meminfo!=""} 查看所有meminfo标签指标
       {psi!=""} 查看所有psi指标
       {vmstat!=""} 查看vmstat指标
       ```

       时间戳转换工具：[Unix 时间戳(Unix timestamp)转换工具 - 时间戳转换工具 (bmcx.com)](https://unixtime.bmcx.com/)

    3. 直接查看 x-monitor 导出的指标

       ```
       curl 0.0.0.0:8000/metrics
       ```

    4. 启动 Prometheus
       ```
       ./prometheus --log.level=debug
       ```

5.  #### 指标说明

    1.  ##### cpu steal

        - 由于服务商在提供虚拟机时存在 CPU 超卖问题，因此和其他人共享 CPU 使用的情况是可能的。
        - 当发生 CPU 使用碰撞情况时，CPU 的使用取决于调度优先级；优先级低的进程会出现一点点 steal 值；若 steal 出现大幅升高，则说明由于超卖问题，把主机 CPU 占用满了。
        - 不使用虚拟机时一般不用关心该指标；使用虚拟机时，steal 代表超卖的幅度，一般不为 0 。

    2.  ##### slab

        内核使用的所有内存片。

        - 可回收 reclaimable。
        - 不可回收 unreclaimable。

    3.  ##### cache 和 buffer

        - 操作系统尚未 flush 的写入数据，可以被读取，对应 dirty cache。
        - 可以近似认为是一样的东西。cache 对应块对象，底层是 block 结构，4k；buffer 对应文件对象，底层是 dfs 结构。可以粗略的认为 cache+buffer 是总的缓存。

    4.  ##### disk

        - storage inode util：小文件过多，导致 inode 耗光。
        - device/disk util：磁盘总 IO 量和理论上可行的 IO 量的比值，一般来说 util 越高，IO 时间越长。
        - 当 disk util 达到 100%，表示的不是 IO 性能低，而是 IO 需要排队，此时 CPU 使用看起来是下跌的，此时 cpu 的 iowait 会升高。

    5.  ##### memory

        - used = total - free - buffer - cache - slab reclaimable

        - util = used / total

          如果 util 操过 50%则认为是有问题的。若是 IO 密集型应用，在 util 操过 50%后一定要注意。

    6.  ##### PSI(Pressure Stall Information)

        使用的 load average 有几个缺点

        - load average 的计算包含了 TASK_RUNNING 和 TASK_UNINTERRUPTIBLE 两种状态的进程，TASK_RUNNING 是进程处于运行，或等待分配 CPU 的准备运行状态，TASK_UNINTERRUPTIBLE 是进程处于不可中断的等待，一般是等待磁盘的输入输出。因此 load average 的飙高可能是因为 CPU 资源不够，让很多 TASK_RUNNING 状态的进程等待 CPU，也可能是由于磁盘 IO 资源紧张，造成很多进程因为等待 IO 而处于 TASK_UNINTERRUPTIBLE 状态。可以通过 load average 发现系统很忙，但是无法区分是因为争夺 CPU 还是 IO 引起的。
        - load average 最短的时间窗口是 1 分钟。
        - load average 报告的是活跃进程的原始数据，还需要知道可用 CPU 核数，这样 load average 的值才有意义。

        当 CPU、内存或 IO 设备争夺激烈的时候，系统会出现负载的延迟峰值、吞吐量下降，并可能触发内核的 `OOM Killer`。PSI 字面意思就是由于资源（CPU、内存和 IO）压力造成的任务执行停顿。**PSI** 量化了由于硬件资源紧张造成的任务执行中断，统计了系统中任务等待硬件资源的时间。我们可以用 **PSI** 作为指标，来衡量硬件资源的压力情况。停顿的时间越长，说明资源面临的压力越大。PSI 已经包含在 4.20 及以上版本内核中。https://xie.infoq.cn/article/931eee27dabb0de906869ba05。

        开启 psi：

        - 查看所有内核启动，grubby --info=ALL

        - 增加内核启动参数：grubby --update-kernel=/boot/vmlinuz-4.18.0 **--args=psi=1**，重启系统。

        - 查看 PSI 结果：

              	tail /proc/pressure/*
              	==> /proc/pressure/cpu <==
              	some avg10=0.00 avg60=0.55 avg300=0.27 total=1192936
              	==> /proc/pressure/io <==
              	some avg10=0.00 avg60=0.13 avg300=0.06 total=325847
              	full avg10=0.00 avg60=0.03 avg300=0.01 total=134192
              	==> /proc/pressure/memory <==
              	some avg10=0.00 avg60=0.00 avg300=0.00 total=0
              	full avg10=0.00 avg60=0.00 avg300=0.00 total=0

    7.  ##### 网络

        1. 网卡

           /proc/net/dev：网卡指标文件

           /sys/class/net/：该目录下会有所有网卡的子目录，目录下包含了网口的配置信息，包括 deviceid，状态等。

           /sys/devices/virtual/net/：目录下都是虚拟网卡，通过该目录可以区分系统中哪些是虚拟网卡

           命令：ip -s -s link，查看所有设备的状态、统计信息

        2. 协议统计，/proc/net/netstat，/proc/net/snmp

           1. ECN。拥塞重传包，TCP通过发送端和接收端以及**中间路由器**的配合，感知中间路径的拥塞，并主动减缓TCP的丢包决策。其实使用和路由器配合的方式，TCP将网络路径中所有转发设备看做是黑盒，中间路由器如果过载丢包，发送端TCP是没法感知的，只有在定时器超时之后，而这个定时器相对较长，通常几秒到几十秒不等。TCP现有的拥塞控制：慢启动、快输重传、快速恢复。
    
                 +-----+-----+
    
                 | ECN FIELD |
    
                 +-----+-----+
    
              ​    ECT  CE    [Obsolete] RFC 2481 names for the ECNbits.
    
              ​    0  0    Not-ECT
    
              ​    0  1    ECT(1)
    
              ​    1  0    ECT(0)
    
              ​    1  1    CE
    
           2. SYN Cookie。该技术对于超过backlog长度的SYN包使用cookie技术，可以让服务器收到客户端SYN报文时，不分配资源保存客户端信息，而是将这些信息保存在SYN+ACK的初始序号和时间戳中。对正常的连接，这些信息会随着ACK报文被带回来。该特性一般不会触发，只有 tcp_max_syn_backlog队列占满时才会。
    
           3. Reorder。当发现了需要更新某条TCP流的reordering值（乱序值）时，会可能使用4种乱序计数器。
    
           4. TCP OFO（Out-Of-Order）。乱序的数据包被保存在TCP套接口的out_of_order_queue队列（实际是个红黑树）中。原因一般因为网络拥塞，导致顺序包抵达时间不同，延时太长，或者丢包，需要重新组合数据单元，因为数据包可能由不同的路径到达。
    
        3. 套接字，/proc/net/socksat
    
        4. 连接跟踪
    
    8.  ##### 背景知识
    
        1.  缺页错误：malloc是扩展虚拟地址空间，应用程序使用store/load来使用分配的内存地址，这就用到虚拟地址到物理地址的转换。该虚拟地址没有实际对应的物理地址，这会导致MMU产生一个错误page fault。
    
        2.  RSS。进程所使用的全部物理内存数量称为常驻集大小（RSS）。
    
        3.  SWAP。当系统内存需求超过一定水平时，内核中kswapd就开始寻找可以释放的内存。
    
            1.  文件系统页，从磁盘中读取并且没有修改过的页（backed by disk，磁盘有备份的页），例如：可执行代码、文件系统的元数据。
            2.  被修改过的文件系统页，就是dirty page，这些页要先写回磁盘才可以被释放。
            3.  应用程序内存页，这些页被称为匿名页（anonymous memory），因为这些页不是来源于某个文件。如果系统中有换页设备（swap分区），那么这些页可以先存入换页设备。
            4.  内存不够时，将页换页到换页设备上这一般会导致应用程序运行速度大幅下降。有些生产系统根本不配置换页设备。当没有换页设备时，系统出现内存不足情况，内核就会调用内存溢出进程终止程序杀掉某个进程。
    
        4.  memory的buff、cache区别。buff是存储I/O缓冲区占用的内存，cache是文件系统缓存占用的内存。
    
        5.  Out of socket memory。两种情况会发生
    
            1.  有很多孤儿套接字(orphan sockets)
            2.  tcp用尽了给他分配的内存。
    
            查看内核分配了多少内存给TCP，这里的单位是page，4096bytes
    
            ```
            [calmwu@192 build]$ cat /proc/sys/net/ipv4/tcp_mem 
            187683	250244	375366
            ```
    
            当 tcp 使用的 page 少于 187683 时，kernel 不对其进行任何的干预
    
            当 tcp 使用了超过 250244 的 pages 时，kernel 会进入 “memory pressure”
    
            当 tcp 使用的 pages 超过 375366 时，我们就会看到题目中显示的信息
    
            查看tcp实际使用的内存，实际使用的2，是远小于最低设置的。那么就只有可能是orphan socket导致的了。
    
            ```
            [calmwu@192 build]$ cat /proc/net/sockstat
            sockets: used 672
            TCP: inuse 9 orphan 0 tw 0 alloc 14 mem 2
            UDP: inuse 6 mem 1
            UDPLITE: inuse 0
            RAW: inuse 0
            FRAG: inuse 0 memory 0
            ```
    
            
