#!/bin/bash
while [ true ]; do
/usr/bin/sleep 1
netstat -n  >> ns.log
done