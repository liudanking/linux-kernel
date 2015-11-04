#!/bin/bash
sysctl net.ipv4.tcp_congestion_control=cubic
lsmod && rmmod tcp_ngg
insmod net/ipv4/tcp_ngg.ko
sysctl net.ipv4.tcp_congestion_control=ngg


