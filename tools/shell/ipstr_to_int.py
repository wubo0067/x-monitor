#!/usr/bin/env python3
import socket
import struct


def ip2int(addr):
    return struct.unpack("!I", socket.inet_aton(addr))[0]


def int2ip(addr):
    return socket.inet_ntoa(struct.pack("!I", addr))


print(int2ip(2181212352))
print(int2ip(socket.ntohl(2181212352)))
print(socket.htonl(ip2int('10.88.0.125'))) #

print(socket.htonl(173539453))