1. 安装 systemtap 依赖的 kernel 包

   ```
   dnf -y install kernel-devel-4.19.90-89.11.v2401.ky10.x86_64 kernel-headers-4.19.90-89.11.v2401.ky10.x86_64
   dnf -y install kernel-debug-4.19.90-89.11.v2401.ky10.x86_64 kernel-debug-core-4.19.90-89.11.v2401.ky10.x86_64
   dnf -y install kernel-debuginfo-4.19.90-89.11.v2401.ky10.x86_64
   ```

2. 启动命令

   ```
   stap -g -s 16 -v ./sock_accept.stp 0 3
   ```

   0：表示不过滤进程pid

   3：表示report的时间间隔

3. 输出，定时输出某个进程间隔时间内accept次数，全连接队队列（最小：平均：最大：limit）数量，半链接队列（最大：limit）数量

   ```
   Tue Aug 20 20:03:19 2024>>>>>>
   	pid:80473, execname:redis-server, accepts:220, full_conn_que(min:0,avg:15,max:40,limit:128) half_conn_queue(max:0,limit:2048) in (3)secs
   <<<<<<
   Tue Aug 20 20:03:22 2024>>>>>>
   	pid:80473, execname:redis-server, accepts:791, full_conn_que(min:0,avg:9,max:50,limit:128) half_conn_queue(max:0,limit:2048) in (3)secs
   <<<<<<
   Tue Aug 20 20:03:25 2024>>>>>>
   	pid:80473, execname:redis-server, accepts:749, full_conn_que(min:0,avg:11,max:59,limit:128) half_conn_queue(max:0,limit:2048) in (3)secs
   <<<<<<
   Tue Aug 20 20:03:28 2024>>>>>>
   	pid:80473, execname:redis-server, accepts:880, full_conn_que(min:0,avg:10,max:45,limit:128) half_conn_queue(max:0,limit:2048) in (3)secs
   <<<<<<
   Tue Aug 20 20:03:31 2024>>>>>>
   	pid:80473, execname:redis-server, accepts:791, full_conn_que(min:0,avg:10,max:54,limit:128) half_conn_queue(max:0,limit:2048) in (3)secs
   <<<<<<
   ```

   
