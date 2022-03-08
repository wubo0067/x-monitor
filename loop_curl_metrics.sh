#!/bin/bash

while :
do
  curl 0.0.0.0:8000/metrics
  sleep 1
done
