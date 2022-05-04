#!/usr/bin/env python3
import socket
import struct
import sys


def ip2int(addr):
    return struct.unpack("!I", socket.inet_aton(addr))[0]


def int2ip(addr):
    return socket.inet_ntoa(struct.pack("!I", addr))


# print(int2ip(2181212352))
print(int2ip(socket.ntohl(1050633482)))
# print(socket.htonl(ip2int('10.88.0.125')))

# print(socket.htonl(173539453))

if __name__ == "__main__":
    print("convert ip '%s' to net num: %d" %
          (sys.argv[1], socket.htonl(ip2int(sys.argv[1]))))
