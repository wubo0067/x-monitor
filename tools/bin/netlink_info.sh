#!/bin/bash

awk 'NR>1 {
   # 获取pid和inode
   pid=$3
   inode=$10
   
   # 获取进程名
   if(pid != "0") {
       cmd="ps -p " pid " -o comm= 2>/dev/null"
       cmd | getline pname
       close(cmd)
   } else {
       pname="kernel"
   }
   
   # 获取socket文件信息
   if(pid != "0") {
       cmd="ls -l /proc/" pid "/fd 2>/dev/null | grep \"socket:\\[" inode "\\]\""
       cmd | getline socket_info
       close(cmd)
       if(socket_info == "") {
           socket_info = "N/A"
       }
   } else {
       socket_info = "N/A"
   }
   
   # 输出结果
   printf "PID: %5s  Process: %-15s  Inode: %-8s  Socket: %s\n", 
          pid, pname, inode, socket_info
   
}' /proc/net/netlink
