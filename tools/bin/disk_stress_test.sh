#!/bin/bash

while true
do
    echo "dd write file"
    dd if=/dev/zero of=/var/log/x-monitor/test.dbf bs=8k count=100000 oflag=sync
    sleep 1
    rm /var/log/x-monitor/test.dbf
done