#!/usr/bin/env bash

IFACE=veth1

install() {
    modprobe dummy
    lsmod | grep dummy
    if [ $? -neq 0 ]; then
        exit 1
    fi

    ip link add $IFACE type dummy
    ip link delete $IFACE type dummy
}

set -x
install

ip address change dev eth10 10.0.0.1
set +x
