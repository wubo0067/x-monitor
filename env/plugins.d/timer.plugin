#!/bin/bash

runtime="5 minute"
endtime=$(date -ud "$runtime" +%s)

while [[ $(date -u +%s) -le $endtime ]]
do
    printf "Time Now: `date +%H:%M:%S`\n"
    printf "Sleeping for 10 seconds\n"
    sleep 10
done