#!/bin/bash

# $1 should be sudo executable
"$1" ip link del czechlight0 type dummy || true
