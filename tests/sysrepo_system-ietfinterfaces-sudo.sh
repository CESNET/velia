#!/usr/bin/env bash

# $1 should be sudo executable
"$1" ip link del czechlight0 type dummy || true
"$1" ip link del czechlight_br0 type dummy || true
