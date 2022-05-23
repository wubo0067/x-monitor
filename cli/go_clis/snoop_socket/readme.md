# 使用bpftrace定位程序的socket系统调用

### 编写测试程序

该程序定时调用syscall.Socket去创建套接字。

	for {
		select {
		case <-ctx.Done():
			glog.Info("stop bpftrace socket")
			break L
		case <-time.After(time.Second * 3):
			fd, err := syscall.Socket(syscall.AF_INET, syscall.SOCK_STREAM, syscall.IPPROTO_IP)
			if err != nil {
				glog.Errorf("create socket error: %s", err.Error())
			} else {
				fd_slice = append(fd_slice, fd)
				glog.Infof("create socket fd: %d", fd)
			}
		}
	}

通过lsof可以观察进程的句柄，可见类型sock的句柄是不断在增长的。

```
snoop_soc 2756859 root    7u     sock    0,8      0t0 18122356 protocol: TCP
snoop_soc 2756859 root    8u     sock    0,8      0t0 18122368 protocol: TCP
snoop_soc 2756859 root    9u     sock    0,8      0t0 18122511 protocol: TCP
snoop_soc 2756859 root   10u     sock    0,8      0t0 18122569 protocol: TCP
```

### 编写bpftrace脚本

该脚本通过hook kprobe:__x64_sys_socket来定位获取进程创建socket堆栈，通过comm来过滤指定进程

```
#!/usr/bin/env bpftrace

kprobe:__x64_sys_socket /comm == "snoop_socket"/
{
    @[ustack] = count();
}

kprobe:__x64_sys_socketpair /comm == "snoop_socket"/
{
    @[ustack] = count();
}
```

执行脚本，可以看到输出堆栈结果，可见调用了7次

[root@VM-0-8-centos snoop_socket]# ./snoop_socket.bt 

```
Attaching 2 probes...
^C

@[
    syscall.RawSyscall.abi0+22
    syscall.Socket+274
    main.main+668
    runtime.main+472
    runtime.goexit.abi0+1
]: 7
```

### 通过堆栈找到系统调用代码

通过堆栈可以看到main.main + 668，找到指令对应的代码。

1. 使用readelf

   ```
   [root@VM-0-8-centos go_clis]# readelf -sW bin/snoop_socket |grep main.main
    12851: 0000000000dc3240  2282 FUNC    LOCAL  DEFAULT    8 main.main
   ```

2. 使用nm

   ```
   [root@VM-0-8-centos go_clis]# nm bin/snoop_socket |grep main.main
   0000000000dc3240 t main.main
   ```

通过上面的命可以得到main.main的基地址是0000000000dc3240，加上偏移668，结果是0000000000DC34DC。

使用命令如下命令，可以定位到具体的代码行。也就是38行。这个测试代码一致。

```
[root@VM-0-8-centos go_clis]# addr2line -e ./bin/snoop_socket 0000000000DC34DC
/root/x-monitor/cli/go_clis/snoop_socket/main.go:38
```

