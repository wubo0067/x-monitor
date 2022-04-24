#!/bin/bash
# set -e
# set -u 
# set -o pipefail
THISPID=`ps -eo pid,comm | grep $1 | awk '{print $1}'`
cat /proc/$THISPID/cgroup