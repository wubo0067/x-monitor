#!/bin/bash

while :
do
  curl 127.0.0.1:31078/x-monitor/metrics
  sleep 1
done