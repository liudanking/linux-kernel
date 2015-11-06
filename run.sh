#!/bin/bash

# usage:
# ./run.sh run
# ./run.sh setcc cubic
# ./run.sh install


KERNEL_VERSION=4.1.5
TEST_DIR=~/my-kernel-module
INSTALL_DIR=/lib/modules/$KERNEL_VERSION/net/ipv4

run() {
	sysctl net.ipv4.tcp_congestion_control=cubic
	lsmod
	rmmod tcp_ngg 
	mkdir -p $TEST_DIR && \
	cp -vrp net/ipv4/tcp_ngg.ko $TEST_DIR && \
	insmod $TEST_DIR/tcp_ngg.ko && \
	sysctl net.ipv4.tcp_congestion_control=ngg
}

showcc() {
	sysctl net.ipv4.tcp_congestion_control
}

setcc() {
	showcc
	sysctl net.ipv4.tcp_congestion_control=$2
}

install() {
	mkdir -p $INSTALL_DIR
	cp -vrp net/ipv4/tcp_ngg.ko $INSTALL_DIR
	pushd
	cd $INSTALL_DIR
	depmod -a
	popd
}

case "$1" in
	run)
		run
		;;
	showcc)
		showcc
		;;
	setcc)
		setcc
		;;
	install)
		install
		;;
	*)
		run
		;;
esac

