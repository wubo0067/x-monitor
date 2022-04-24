#!/bin/bash

PNAME="$1"
#LOG_FILE="$2"

while true ; do
    echo "$(date) :: $PNAME[$(pidof ${PNAME})] $(ps -C ${PNAME} -o %cpu | tail -1)%" #>> $LOG_FILE
    sleep 2
done