#!/bin/bash

if [ $# -eq 0 ]; then
    ps -eo stat,comm,pid,ppid,lstart,etime
else
    ps -eo stat,comm,pid,ppid,lstart,etime | grep "$1"
fi
