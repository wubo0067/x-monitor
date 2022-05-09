#!/bin/bash

# set -e
# set -u 
# set -o pipefail

package_date_dir="x-monitor."`date +%Y%m%d`
echo ${package_date_dir}
if [ ! -d ${package_date_dir} ]; then
    mkdir -p ${package_date_dir}
    mkdir -p ${package_date_dir}/tools
    mkdir -p ${package_date_dir}/env
    mkdir -p ${package_date_dir}/bin
fi

cp -r tools/* ${package_date_dir}/tools
cp -r env/* ${package_date_dir}/env
cp -r build/bin/x-monitor ${package_date_dir}/bin

tar -czf ${package_date_dir}.tar.gz ${package_date_dir}