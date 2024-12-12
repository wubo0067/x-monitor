1. 查看semaphore初始值

   ```
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test 
   rw_semaphore: 000000008d1a177d, count:0x0000000000000000
   ```

2. 执行一个down_read，查看semaphore结果

   ```
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "down_read" > /proc/cw_test/rw_semaphore_test
     ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test               
   rw_semaphore: 000000008d1a177d, count:0x0000000000000100
   ```

   可以看到，count变为0x100，这是符合代码的，初始值0加上RWSEM_READER_BIAS，

   ```
   #define RWSEM_READER_SHIFT 8
   #define RWSEM_READER_BIAS (1UL << RWSEM_READER_SHIFT)
   ```

3. 执行多个down_read，查看semaphore，可见count = 0x0300

   ```
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "down_read" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "down_read" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "down_read" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test               
   rw_semaphore: 000000008d1a177d, count:0x0000000000000300
   ```

4. 在执行一个write_down，这时写线程会进入D状态，同时进入等待队列

   ```
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "down_write" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test                
   rw_semaphore: 000000008d1a177d, count:0x0000000000000302
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'(efault)', type:'RWSEM_WAITING_FOR_READ'
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  ps -eo comm,pid,stat|grep cw
   cw_sem_downwrit  406134 D
   ```

5. 释放3个read之后，会发现count=0x1，只有一个write lock了

   ```
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "up_read" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test             
   rw_semaphore: 000000008d1a177d, count:0x0000000000000202
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'(efault)', type:'RWSEM_WAITING_FOR_READ'
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "up_read" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test             
   rw_semaphore: 000000008d1a177d, count:0x0000000000000102
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'(efault)', type:'RWSEM_WAITING_FOR_READ'
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "up_read" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test             
   rw_semaphore: 000000008d1a177d, count:0x0000000000000001
   ```

   **count=0x01表明有write lock**，\#define RWSEM_WRITER_LOCKED (1UL << 0)

6. 继续三个write lock，这三个会排队，实际共有4个write lock，发现计数器是不会变化的，但是count=0x03，应为count = RWSEM_FLAG_WAITERS|RWSEM_FLAG_WAITERS，\#define RWSEM_FLAG_WAITERS (1UL << 1)

   ```
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test                
   rw_semaphore: 000000008d1a177d, count:0x0000000000000003
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'(efault)', type:'RWSEM_WAITING_FOR_READ'
   ```

   这时有三个D状态的线程

   ```
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  ps -eo comm,pid,stat|grep cw                    
   cw_sem_downwrit  406301 D
   cw_sem_downwrit  406319 D
   cw_sem_downwrit  406341 D
   ```

7. 继续两个read lock，会发现count值不会变，但是read 线程会排队到wait list

   ```
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "down_read" > /proc/cw_test/rw_semaphore_test
   ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  echo "down_read" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  /lib/modules/4.18.0-425.19.2.el8_7.x86_64/extra/calmwu_modules  cat /proc/cw_test/rw_semaphore_test               
   rw_semaphore: 000000008d1a177d, count:0x0000000000000003
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'cw_sem_downread', type:'RWSEM_WAITING_FOR_READ'
   	wait list: comm:'cw_sem_downread', type:'RWSEM_WAITING_FOR_READ'
   	wait list: comm:'(efault)', type:'RWSEM_WAITING_FOR_READ'
   ```

8. 添加了semaphore的owner显示

   ```
    ⚡ root@localhost  ~   cat /proc/cw_test/rw_semaphore_test                
   rw_semaphore:0000000069fe9abc, count:0x0000000000000001, owner:00000000e19321b6, owner.comm:'cw_sem_downwrit'
    ⚡ root@localhost  ~  echo "down_write" > /proc/cw_test/rw_semaphore_test
    ⚡ root@localhost  ~  cat /proc/cw_test/rw_semaphore_test                
   rw_semaphore:0000000069fe9abc, count:0x0000000000000003, owner:00000000e19321b6, owner.comm:'cw_sem_downwrit'
   	wait list: comm:'cw_sem_downwrit', type:'RWSEM_WAITING_FOR_WRITE'
   	wait list: comm:'(efault)', type:'RWSEM_WAITING_FOR_READ'
   ```

   