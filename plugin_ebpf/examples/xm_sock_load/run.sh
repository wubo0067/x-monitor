#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

BPFFS=/sys/fs/bpf
LINK_PIN=$BPFFS/xm_sock_flags

function config_device {
	ip netns add at_ns0
	ip link add veth0 type veth peer name veth0b
	ip link set veth0b up
	ip link set veth0 netns at_ns0
	ip netns exec at_ns0 ip addr add 172.16.1.100/24 dev veth0
	ip netns exec at_ns0 ip addr add 2401:db00::1/64 dev veth0 nodad
	ip netns exec at_ns0 ip link set dev veth0 up
	ip addr add 172.16.1.101/24 dev veth0b
	ip addr add 2401:db00::2/64 dev veth0b nodad
}

function config_cgroup {
	rm -rf /tmp/cgroupv2
	mkdir -p /tmp/cgroupv2
	mount -t cgroup2 none /tmp/cgroupv2
	mkdir -p /tmp/cgroupv2/foo
	echo $$ >> /tmp/cgroupv2/foo/cgroup.procs
}

function config_bpffs {
	if mount | grep $BPFFS > /dev/null; then
		echo "bpffs already mounted"
	else
		echo "bpffs not mounted. Mounting..."
		mount -t bpf none $BPFFS
	fi
}

function attach_bpf {
	echo "Executing: ./xm_sock_load -progFilterID=$1 --alsologtostderr -v=4 -stderrthreshold=INFO"
	./xm_sock_load -progFilterID=$1 --alsologtostderr -v=4 -stderrthreshold=INFO
	[ $? -ne 0 ] && exit 1
}

function cleanup {
	rm -rf $LINK_PIN
	ip link del veth0b
	ip netns delete at_ns0
	umount /tmp/cgroupv2
	rm -rf /tmp/cgroupv2
}

cleanup 2>/dev/null

set -e
config_device
config_cgroup
config_bpffs
set +e

#
# Test 1 - fail ping6
#
attach_bpf 1

echo "**1** ping -c1 -w1 172.16.1.100 ---> ok"
ping -c1 -w1 172.16.1.100
if [ $? -ne 0 ]; then
	echo "ping failed when it should succeed"
	cleanup
	exit 1
fi

echo "**2** ping6 -c1 -w1 2401:db00::1 ---> fail"
ping6 -c1 -w1 2401:db00::1
if [ $? -eq 0 ]; then
	echo "ping6 succeeded when it should not"
	cleanup
	exit 1
fi

echo "fail ping6 test completed, removing cgroup/sock1"

rm -rf $LINK_PIN
sleep 1                 # Wait for link detach
# 判断$LINK_PIN 是否存在
if [ -e $LINK_PIN ]; then
	echo "$LINK_PIN exists"
else
	echo "$LINK_PIN has been removed"
fi

#
# Test 2 - fail ping
#
attach_bpf 2
echo "**3** ping6 -c1 -w1 2401:db00::1 ---> ok"
ping6 -c1 -w1 2401:db00::1
if [ $? -ne 0 ]; then
	echo "ping6 failed when it should succeed"
	cleanup
	exit 1
fi

echo "**4** ping -c1 -w1 172.16.1.100 ---> fail"
ping -c1 -w1 172.16.1.100
if [ $? -eq 0 ]; then
	echo "ping succeeded when it should not"
	cleanup
	exit 1
fi

echo "fail ping test completed, removing cgroup/sock2"

cleanup
echo
echo "*** PASS ***"
